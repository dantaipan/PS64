#include "c64api.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* connect() with a timeout: an unreachable C64 would otherwise block for minutes. */
static int connect_timeout(int s, const struct sockaddr_in *addr, int timeout_ms)
{
    int flags = fcntl(s, F_GETFL);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    int rc = connect(s, (const struct sockaddr *)addr, sizeof(*addr));
    if (rc < 0 && errno == EINPROGRESS) {
        struct pollfd pfd = { s, POLLOUT, 0 };
        int err = 0;
        socklen_t len = sizeof(err);
        rc = poll(&pfd, 1, timeout_ms) == 1 &&
             getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0 ? 0 : -1;
    }
    fcntl(s, F_SETFL, flags);
    return rc;
}

#ifdef __PROSPERO__
/* PS5 system resolver, used if libc's getaddrinfo can't resolve. */
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetPoolDestroy(int memid);
int sceNetResolverCreate(const char *name, int memid, int flags);
int sceNetResolverStartNtoa(int rid, const char *hostname, struct in_addr *addr,
                            int timeout, int retry, int flags);
int sceNetResolverDestroy(int rid);

static int sce_resolve(const char *host, struct in_addr *out)
{
    int memid = sceNetPoolCreate("ps64", 4 * 1024, 0);
    if (memid < 0)
        return -1;
    int rid = sceNetResolverCreate("ps64", memid, 0);
    int rc = rid >= 0 ? sceNetResolverStartNtoa(rid, host, out, 0, 0, 0) : -1;
    if (rid >= 0)
        sceNetResolverDestroy(rid);
    sceNetPoolDestroy(memid);
    return rc < 0 ? -1 : 0;
}
#endif

/* IPv4 address for an IP string or host name. */
static int resolve(const char *host, struct in_addr *out)
{
    if (inet_pton(AF_INET, host, out) == 1)
        return 0;
#ifdef PS64_NATIVE
    return sce_resolve(host, out);          /* getaddrinfo isn't usable in a native app */
#endif
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) == 0 && res) {
        *out = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
        return 0;
    }
#ifdef __PROSPERO__
    return sce_resolve(host, out);
#else
    return -1;
#endif
}

static int connect_to(const char *host, int port, int timeout_ms, int recv_timeout_s)
{
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (resolve(host, &addr.sin_addr) != 0)
        return -1;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct timeval tv = { recv_timeout_s, 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect_timeout(s, &addr, timeout_ms) < 0) {
        close(s);
        return -1;
    }
    return s;
}

int c64api_connect(const char *host, int port, int timeout_ms)
{
    return connect_to(host, port, timeout_ms, 3);
}

static int send_all(int s, const void *buf, size_t len)
{
    const char *p = buf;
    while (len > 0) {
        ssize_t n = send(s, p, len, 0);
        if (n <= 0)
            return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

#define MAX_RESPONSE (8 * 1024 * 1024)

int c64api_request_alloc(const c64api *api, const char *method, const char *path,
                         const char *content_type, const void *body, size_t body_len,
                         char **resp, size_t *resp_len, char *resp_type, size_t type_cap)
{
    if (resp) *resp = NULL;
    if (resp_len) *resp_len = 0;
    if (resp_type && type_cap) resp_type[0] = 0;
    int port = api->port ? api->port : 80;
    int s = connect_to(api->host, port, api->timeout_s ? 5000 : 2000,
                       api->timeout_s ? api->timeout_s : 3);
    if (s < 0)
        return -1;

    char host[160];
    snprintf(host, sizeof(host), port == 80 ? "%s" : "%s:%d", api->host, port);
    char head[1024];
    int n = snprintf(head, sizeof(head),
        "%s %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: ps64\r\n"
        "%s%s%s%s%s%s%s%s%sContent-Length: %zu\r\n\r\n",
        method, path, host,
        api->password ? "X-Password: " : "", api->password ? api->password : "",
        api->password ? "\r\n" : "",
        api->client_id ? "client-id: " : "", api->client_id ? api->client_id : "",
        api->client_id ? "\r\n" : "",
        content_type ? "Content-Type: " : "", content_type ? content_type : "",
        content_type ? "\r\n" : "", body_len);
    if (n < 0 || (size_t)n >= sizeof(head) || send_all(s, head, (size_t)n) != 0 ||
        (body_len && send_all(s, body, body_len) != 0)) {
        close(s);
        return -1;
    }

    /* HTTP/1.0: the server closes the connection after the response. */
    size_t cap = 4096, got = 0;
    char *buf = malloc(cap + 1);
    while (buf) {
        if (got == cap) {
            if (cap >= MAX_RESPONSE)
                break;
            cap *= 2;
            char *bigger = realloc(buf, cap + 1);
            if (!bigger) break;
            buf = bigger;
        }
        ssize_t r = recv(s, buf + got, cap - got, 0);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(s);
    if (!buf)
        return -1;
    buf[got] = 0;

    int status = -1;
    if (got > 0)
        sscanf(buf, "HTTP/%*s %d", &status);
    char *body_start = strstr(buf, "\r\n\r\n");
    if (body_start && resp_type && type_cap) {
        /* Content-Type from the headers, if present. */
        for (char *h = strstr(buf, "\r\n"); h && h < body_start; h = strstr(h + 2, "\r\n")) {
            if (strncasecmp(h + 2, "Content-Type:", 13) == 0) {
                const char *v = h + 15;
                while (*v == ' ') v++;
                size_t len = strcspn(v, "\r\n");
                snprintf(resp_type, type_cap, "%.*s", (int)len, v);
                break;
            }
        }
    }
    if (resp && body_start) {
        body_start += 4;
        size_t blen = got - (size_t)(body_start - buf);
        memmove(buf, body_start, blen);
        buf[blen] = 0;
        *resp = buf;
        if (resp_len) *resp_len = blen;
        return status;
    }
    free(buf);
    return status;
}

int c64api_request(const c64api *api, const char *method, const char *path,
                   const char *content_type, const void *body, size_t body_len,
                   void *resp, size_t resp_cap, size_t *resp_len)
{
    char *all = NULL;
    size_t len = 0;
    int status = c64api_request_alloc(api, method, path, content_type, body, body_len,
                                      resp ? &all : NULL, &len, NULL, 0);
    if (resp_len)
        *resp_len = 0;
    if (resp && all) {
        size_t n = len < resp_cap ? len : resp_cap;
        memcpy(resp, all, n);
        if (resp_len) *resp_len = n;
    }
    free(all);
    return status;
}

int c64api_parse_url(const char *url, char *host, size_t host_cap, int *port,
                     char *base, size_t base_cap)
{
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0)
        p += 7;
    else if (strstr(p, "://"))
        return -1;                          /* only plain http is supported */
    size_t hl = strcspn(p, ":/");
    if (hl == 0 || hl >= host_cap)
        return -1;
    snprintf(host, host_cap, "%.*s", (int)hl, p);
    p += hl;
    *port = 80;
    if (*p == ':') {
        *port = atoi(p + 1);
        if (*port <= 0 || *port > 65535)
            return -1;
        p += strcspn(p, "/");
    }
    size_t bl = strlen(p);
    while (bl && p[bl - 1] == '/')
        bl--;                               /* no trailing slash */
    snprintf(base, base_cap, "%.*s", (int)bl, p);
    return 0;
}

int c64api_local_ip(const char *host, char *out, size_t outlen)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(80);
    if (inet_pton(AF_INET, host, &dst.sin_addr) != 1 ||
        connect(s, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        close(s);
        return -1;
    }
    struct sockaddr_in me;
    socklen_t len = sizeof(me);
    int ok = getsockname(s, (struct sockaddr *)&me, &len) == 0 &&
             inet_ntop(AF_INET, &me.sin_addr, out, (socklen_t)outlen) != NULL;
    close(s);
    return ok ? 0 : -1;
}

int c64api_start_stream(const c64api *api, const char *stream, int port, int default_port)
{
    char me[INET_ADDRSTRLEN];
    if (c64api_local_ip(api->host, me, sizeof(me)) != 0) {
        fprintf(stderr, "can't find a local address that reaches %s\n", api->host);
        return -1;
    }
    char path[128];
    if (port == default_port)
        snprintf(path, sizeof(path), "/v1/streams/%s:start?ip=%s", stream, me);
    else
        snprintf(path, sizeof(path), "/v1/streams/%s:start?ip=%s:%d", stream, me, port);
    int status = c64api_request(api, "PUT", path, NULL, NULL, 0, NULL, 0, NULL);
    printf("start %s stream on %s -> %s: HTTP %d\n", stream, api->host, me, status);
    return status >= 200 && status < 300 ? 0 : -1;
}

void c64api_stop_stream(const c64api *api, const char *stream)
{
    char path[64];
    snprintf(path, sizeof(path), "/v1/streams/%s:stop", stream);
    int status = c64api_request(api, "PUT", path, NULL, NULL, 0, NULL, 0, NULL);
    printf("stop %s stream: HTTP %d\n", stream, status);
}

int c64api_json_string(const char *body, const char *key, char *out, size_t cap)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(body, pat);
    if (!p || cap == 0)
        return -1;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != ':')
        return -1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p++ != '"')
        return -1;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < cap) {
        if (*p == '\\' && p[1])
            p++;
        out[n++] = *p++;
    }
    out[n] = 0;
    return 0;
}

void c64api_url_encode(const char *in, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 4 < cap; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            strchr("/-_.~", *p)) {
            out[n++] = (char)*p;
        } else {
            out[n++] = '%';
            out[n++] = hex[*p >> 4];
            out[n++] = hex[*p & 15];
        }
    }
    out[n] = 0;
}
