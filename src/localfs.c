#include "localfs.h"

#include "c64files.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

void localfs_real_path(const char *root, const char *path, char *out, size_t cap)
{
    size_t n = (size_t)snprintf(out, cap, "%s", root);
    while (n > 1 && out[n - 1] == '/')
        out[--n] = 0;
    /* Copy the parts of path one by one, leaving out "", "." and "..". */
    const char *p = path ? path : "";
    while (*p) {
        while (*p == '/')
            p++;
        size_t len = strcspn(p, "/");
        if (len && !(len == 1 && p[0] == '.') && !(len == 2 && p[0] == '.' && p[1] == '.') &&
            n + 1 + len < cap) {
            out[n++] = '/';
            memcpy(out + n, p, len);
            n += len;
            out[n] = 0;
        }
        p += len;
    }
}

/*
 * A native PS5 app can open and read files under /data but not list folders
 * (opendir and stat fail with EPERM). So when a folder can't be listed, the
 * listing comes from "games.txt" in the games folder instead: one path per
 * line, relative to the games folder ("Folder/Game.d64"), an optional TAB and
 * the size after it. tools/make_games_list.sh writes it.
 */
#define MANIFEST_NAME "games.txt"

static char *manifest;             /* cached text of games.txt, reread when "/" is listed */
static char manifest_root[1024];

static int manifest_load(const char *root, int reload)
{
    if (manifest && !reload && !strcmp(manifest_root, root))
        return 0;
    free(manifest);
    manifest = NULL;
    char file[1100];
    snprintf(file, sizeof(file), "%s/" MANIFEST_NAME, root);
    FILE *f = fopen(file, "rb");
    if (!f)
        return -1;
    size_t cap = 1 << 16, len = 0;
    char *buf = malloc(cap);
    size_t n;
    while (buf && (n = fread(buf + len, 1, cap - len - 1, f)) > 0) {
        len += n;
        if (cap - len < 2) {
            char *bigger = realloc(buf, cap *= 2);
            if (!bigger) {
                free(buf);
                buf = NULL;
            } else {
                buf = bigger;
            }
        }
    }
    fclose(f);
    if (!buf)
        return -1;
    buf[len] = 0;
    manifest = buf;
    snprintf(manifest_root, sizeof(manifest_root), "%s", root);
    return 0;
}

static int manifest_list(const char *root, const char *path, ftp_entry *out, int max)
{
    /* path without surrounding slashes, "" for the root */
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path ? path : "");
    char *d = dir;
    while (*d == '/')
        d++;
    size_t dl = strlen(d);
    while (dl && d[dl - 1] == '/')
        d[--dl] = 0;
    if (manifest_load(root, dl == 0) != 0)
        return -1;

    int n = 0;
    for (char *line = manifest; *line && n < max;) {
        char *end = line + strcspn(line, "\r\n");
        char saved = *end;
        *end = 0;
        char *rel = line;
        char *tab = strchr(rel, '\t');
        unsigned long long size = 0;
        if (tab) {
            *tab = 0;
            size = strtoull(tab + 1, NULL, 10);
        }
        while (*rel == '/' || (rel[0] == '.' && rel[1] == '/'))
            rel += rel[0] == '/' ? 1 : 2;
        if (*rel && *rel != '#' &&
            (dl == 0 || (!strncmp(rel, d, dl) && rel[dl] == '/'))) {
            const char *rest = rel + (dl ? dl + 1 : 0);
            const char *slash = strchr(rest, '/');
            size_t len = slash ? (size_t)(slash - rest) : strlen(rest);
            if (len && rest[0] != '.' && len < sizeof(out[0].name)) {
                int dup = 0;
                if (slash)
                    for (int i = n - 1; i >= 0 && !dup; i--)
                        dup = out[i].is_dir && strlen(out[i].name) == len &&
                              !strncmp(out[i].name, rest, len);
                if (!dup) {
                    ftp_entry *e = &out[n++];
                    snprintf(e->name, sizeof(e->name), "%.*s", (int)len, rest);
                    e->is_dir = slash != NULL;
                    e->size = slash ? 0 : size;
                }
            }
        }
        if (tab)
            *tab = '\t';
        *end = saved;
        line = end;
        while (*line == '\r' || *line == '\n')
            line++;
    }
    ftp_sort(out, n);
    return n;
}

int localfs_list(const char *root, const char *path, ftp_entry *out, int max,
                 char *err, size_t errcap)
{
    char dir[1024];
    localfs_real_path(root, path, dir, sizeof(dir));
    DIR *d = opendir(dir);
    if (!d) {
        if (errno != ENOENT) {
            int n = manifest_list(root, path, out, max);
            if (n >= 0)
                return n;
            snprintf(err, errcap, "Can't open %s (no " MANIFEST_NAME " either)", dir);
            return -1;
        }
        snprintf(err, errcap, "Folder not found: %s", dir);
        return -1;
    }
    int n = 0;
    struct dirent *de;
    while (n < max && (de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.' || strlen(de->d_name) >= sizeof(out[0].name))
            continue;
        char full[1200];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
        if (stat(full, &st) != 0)
            continue;
        ftp_entry *e = &out[n++];
        snprintf(e->name, sizeof(e->name), "%s", de->d_name);
        e->is_dir = S_ISDIR(st.st_mode);
        e->size = e->is_dir ? 0 : (unsigned long long)st.st_size;
    }
    closedir(d);
    ftp_sort(out, n);
    return n;
}

int localfs_mkdirs(const char *dir)
{
    char p[1024];
    snprintf(p, sizeof(p), "%s", dir);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0755);
            *s = '/';
        }
    struct stat st;
    return mkdir(p, 0755) == 0 || (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1;
}

int localfs_write(const char *file, const void *data, size_t len)
{
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", file);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = 0;
        if (localfs_mkdirs(dir) != 0)
            return -1;
    }
    FILE *f = fopen(file, "wb");
    if (!f)
        return -1;
    size_t w = fwrite(data, 1, len, f);
    return fclose(f) == 0 && w == len ? 0 : -1;
}

void localfs_safe_name(const char *name, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = name ? name : ""; *p && n + 1 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        out[n++] = c < 32 || c == '/' || c == '\\' || c == ':' ? '_' : (char)c;
    }
    out[n] = 0;
    if (!n || !strcmp(out, ".") || !strcmp(out, ".."))
        snprintf(out, cap, "file");
    else if (out[0] == '.')
        out[0] = '_';
}

int localfs_disk_set(const char *const *names, int n, int chosen, int *order, int max)
{
    if (chosen < 0 || chosen >= n || files_type(names[chosen], 0) != FT_DISK)
        return 0;
    /* Disk images sorted by name (insertion sort: folders hold a few). */
    int count = 0;
    for (int i = 0; i < n && count < max; i++) {
        if (files_type(names[i], 0) != FT_DISK)
            continue;
        int j = count++;
        while (j > 0 && strcasecmp(names[order[j - 1]], names[i]) > 0) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }
    /* Rotate so the chosen disk comes first. */
    int at = 0;
    while (at < count && order[at] != chosen)
        at++;
    if (at == count)
        return 0;
    int tmp[256];
    if (count > 256)
        count = 256;
    for (int i = 0; i < count; i++)
        tmp[i] = order[(at + i) % count];
    memcpy(order, tmp, sizeof(int) * (size_t)count);
    return count;
}

int localfs_write_m3u(const char *file, const char *const *paths, int n)
{
    FILE *f = fopen(file, "w");
    if (!f)
        return -1;
    for (int i = 0; i < n; i++)
        fprintf(f, "%s\n", paths[i]);
    return fclose(f) == 0 ? 0 : -1;
}
