#include "ftp.h"

#include "c64api.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int    fd;
    char   buf[2048];
    size_t len;
} ctrl_conn;

/* Read one line from the control connection (without CRLF). */
static int read_line(ctrl_conn *c, char *line, size_t cap)
{
    for (;;) {
        char *nl = memchr(c->buf, '\n', c->len);
        if (nl) {
            size_t n = (size_t)(nl - c->buf);
            size_t copy = n < cap - 1 ? n : cap - 1;
            memcpy(line, c->buf, copy);
            line[copy] = 0;
            if (copy && line[copy - 1] == '\r')
                line[copy - 1] = 0;
            memmove(c->buf, nl + 1, c->len - n - 1);
            c->len -= n + 1;
            return 0;
        }
        if (c->len == sizeof(c->buf))
            c->len = 0;                     /* overlong line: drop it */
        ssize_t r = recv(c->fd, c->buf + c->len, sizeof(c->buf) - c->len, 0);
        if (r <= 0) {
            if (c->len == 0 || c->len == sizeof(c->buf))
                return -1;
            c->buf[c->len++] = '\n';       /* last line without a newline */
            continue;
        }
        c->len += (size_t)r;
    }
}

/* Read a full reply (multi-line "123-...123 "); returns the code, last line in msg. */
static int read_reply(ctrl_conn *c, char *msg, size_t cap)
{
    char line[512];
    if (read_line(c, line, sizeof(line)) != 0)
        return -1;
    int code = atoi(line);
    if (strlen(line) >= 4 && line[3] == '-') {
        char end[5];
        snprintf(end, sizeof(end), "%.3s ", line);
        do {
            if (read_line(c, line, sizeof(line)) != 0)
                return -1;
        } while (strncmp(line, end, 4) != 0);
    }
    if (msg)
        snprintf(msg, cap, "%s", line);
    return code;
}

static int command(ctrl_conn *c, const char *cmd, char *msg, size_t cap)
{
    char line[600];
    int n = snprintf(line, sizeof(line), "%s\r\n", cmd);
    if (send(c->fd, line, (size_t)n, 0) != n)
        return -1;
    return read_reply(c, msg, cap);
}

int ftp_parse_pasv_port(const char *reply)
{
    const char *p = strchr(reply, '(');
    int h1, h2, h3, h4, p1, p2;
    if (!p || sscanf(p, "(%d,%d,%d,%d,%d,%d)", &h1, &h2, &h3, &h4, &p1, &p2) != 6)
        return -1;
    return p1 * 256 + p2;
}

int ftp_parse_list_line(const char *line, ftp_entry *e)
{
    /* drwxr-xr-x 1 user ftp 0 Jan 01 1980 name with spaces */
    const char *p = line;
    char perms[16];
    int field = 0;
    memset(e, 0, sizeof(*e));
    while (field < 8) {
        while (*p == ' ') p++;
        if (!*p)
            return -1;
        const char *start = p;
        while (*p && *p != ' ') p++;
        if (field == 0)
            snprintf(perms, sizeof(perms), "%.*s", (int)(p - start), start);
        else if (field == 4)
            e->size = strtoull(start, NULL, 10);
        field++;
    }
    if (*p == ' ')
        p++;                                /* exactly one separator before the name */
    if (!*p)
        return -1;
    snprintf(e->name, sizeof(e->name), "%s", p);
    e->is_dir = perms[0] == 'd';
    return 0;
}

static int entry_cmp(const void *a, const void *b)
{
    const ftp_entry *x = a, *y = b;
    if (x->is_dir != y->is_dir)
        return y->is_dir - x->is_dir;
    return strcasecmp(x->name, y->name);
}

void ftp_sort(ftp_entry *e, int n)
{
    qsort(e, (size_t)n, sizeof(*e), entry_cmp);
}

struct ftp_session {
    ctrl_conn c;
    char      host[64];
    int       broken;                       /* control connection out of sync */
};

ftp_session *ftp_open(const char *host, const char *password, char *err, size_t errcap)
{
    return ftp_open_port(host, 21, password, err, errcap);
}

ftp_session *ftp_open_port(const char *host, int port, const char *password, char *err, size_t errcap)
{
    ftp_session *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    snprintf(s->host, sizeof(s->host), "%s", host);
    s->c.fd = c64api_connect(host, port, 2000);
    char msg[512], cmd[600];
    if (s->c.fd < 0) {
        snprintf(err, errcap, "Can't connect - turn on the C64's FTP service");
        free(s);
        return NULL;
    }
    if (read_reply(&s->c, msg, sizeof(msg)) != 220) {
        snprintf(err, errcap, "FTP greeting failed");
        goto fail;
    }
    snprintf(cmd, sizeof(cmd), "USER %s", password ? "" : "anonymous");
    int code = command(&s->c, cmd, msg, sizeof(msg));
    if (code == 331) {
        snprintf(cmd, sizeof(cmd), "PASS %s", password ? password : "anonymous@");
        code = command(&s->c, cmd, msg, sizeof(msg));
    }
    if (code != 230) {
        snprintf(err, errcap, "FTP login refused (%d)", code);
        goto fail;
    }
    command(&s->c, "TYPE I", msg, sizeof(msg));
    return s;
fail:
    close(s->c.fd);
    free(s);
    return NULL;
}

void ftp_close(ftp_session *s)
{
    if (!s)
        return;
    if (!s->broken)
        command(&s->c, "QUIT", NULL, 0);
    close(s->c.fd);
    free(s);
}

int ftp_list_dir(ftp_session *s, const char *dir, ftp_entry *out, int max,
                 char *err, size_t errcap)
{
    char msg[512], cmd[600];
    if (s->broken) {
        snprintf(err, errcap, "FTP connection lost");
        return -1;
    }
    snprintf(cmd, sizeof(cmd), "CWD %s", dir);
    int code = command(&s->c, cmd, msg, sizeof(msg));
    if (code != 250) {
        if (code < 0) s->broken = 1;
        snprintf(err, errcap, "Can't open %s", dir);
        return -1;
    }
    code = command(&s->c, "PASV", msg, sizeof(msg));
    if (code != 227) {
        if (code < 0) s->broken = 1;
        snprintf(err, errcap, "FTP passive mode failed");
        return -1;
    }
    /* Connect to the C64's own address: the one in the PASV reply may not be reachable. */
    int port = ftp_parse_pasv_port(msg);
    int data = port > 0 ? c64api_connect(s->host, port, 2000) : -1;
    if (data < 0) {
        s->broken = 1;                      /* server waits on a data connection that never came */
        snprintf(err, errcap, "FTP data connection failed");
        return -1;
    }
    code = command(&s->c, "LIST", msg, sizeof(msg));
    if (code != 150 && code != 125) {
        close(data);
        if (code < 0) s->broken = 1;
        snprintf(err, errcap, "FTP LIST failed (%d)", code);
        return -1;
    }

    ctrl_conn d = { .fd = data };
    char line[512];
    int count = 0;
    while (read_line(&d, line, sizeof(line)) == 0) {
        ftp_entry e;
        if (ftp_parse_list_line(line, &e) != 0 || !strcmp(e.name, ".") || !strcmp(e.name, ".."))
            continue;
        if (count < max)
            out[count++] = e;
    }
    close(data);
    if (read_reply(&s->c, msg, sizeof(msg)) < 0)   /* 226 transfer complete */
        s->broken = 1;
    ftp_sort(out, count);
    return count;
}

int ftp_list(const char *host, const char *password, const char *dir,
             ftp_entry *out, int max, char *err, size_t errcap)
{
    ftp_session *s = ftp_open(host, password, err, errcap);
    if (!s)
        return -1;
    int n = ftp_list_dir(s, dir, out, max, err, errcap);
    ftp_close(s);
    return n;
}
