#include "localfs.h"

#include "c64files.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
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

/* Add one folder entry (skipping dot-files and names too long to keep). */
static void add_entry(const char *dir, const char *name, ftp_entry *out, int *n)
{
    if (name[0] == '.' || strlen(name) >= sizeof(out[0].name))
        return;
    char full[1200];
    struct stat st;
    snprintf(full, sizeof(full), "%s/%s", dir, name);
    if (stat(full, &st) != 0)
        return;
    ftp_entry *e = &out[(*n)++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->is_dir = S_ISDIR(st.st_mode);
    e->size = e->is_dir ? 0 : (unsigned long long)st.st_size;
}

#ifdef PS64_NATIVE
/*
 * The native app's libc can't opendir() in its sandbox (EPERM). Read the folder's
 * entries directly instead: open() without O_DIRECTORY and getdents() into a large,
 * aligned buffer works on the PS5 (fw 13.42; with O_DIRECTORY getdents() gave EINVAL).
 * getdirentries() and the console's own calls are kept as fallbacks; failures go to
 * the log. Returns the count, or -1.
 */
int sceKernelOpen(const char *path, int flags, int mode);
int sceKernelClose(int fd);
int sceKernelGetdents(int fd, char *buf, int nbytes);

static int parse_dents(const char *dir, const char *buf, int got, ftp_entry *out, int *n, int max)
{
    for (int at = 0; at < got && *n < max;) {
        const struct dirent *de = (const struct dirent *)(buf + at);
        if (!de->d_reclen)
            break;
        add_entry(dir, de->d_name, out, n);
        at += de->d_reclen;
    }
    return 0;
}

static int list_raw(const char *dir, ftp_entry *out, int max)
{
    static char buf[65536] __attribute__((aligned(4096)));
    int n = 0, got;
    /* 1. open + getdents. */
    int fd = open(dir, O_RDONLY);
    if (fd >= 0) {
        while (n < max && (got = getdents(fd, buf, sizeof(buf))) > 0)
            parse_dents(dir, buf, got, out, &n, max);
        if (got == 0 || n) {
            close(fd);
            return n;
        }
        printf("localfs: %s: getdents: %s\n", dir, strerror(errno));
        /* 2. getdirentries on the same descriptor. */
        long base = 0;
        lseek(fd, 0, SEEK_SET);
        while (n < max && (got = getdirentries(fd, buf, sizeof(buf), &base)) > 0)
            parse_dents(dir, buf, got, out, &n, max);
        int ok = got == 0 || n;
        if (!ok)
            printf("localfs: %s: getdirentries: %s\n", dir, strerror(errno));
        close(fd);
        if (ok)
            return n;
    } else {
        printf("localfs: %s: open: %s\n", dir, strerror(errno));
    }
    /* 3. The console's own calls (they return 0x8002xxxx error codes). */
    fd = sceKernelOpen(dir, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        printf("localfs: %s: sceKernelOpen: 0x%08x\n", dir, (unsigned)fd);
        return -1;
    }
    while (n < max && (got = sceKernelGetdents(fd, buf, sizeof(buf))) > 0)
        parse_dents(dir, buf, got, out, &n, max);
    if (got < 0 && !n)
        printf("localfs: %s: sceKernelGetdents: 0x%08x\n", dir, (unsigned)got);
    sceKernelClose(fd);
    return got < 0 && !n ? -1 : n;
}
#endif

int localfs_list(const char *root, const char *path, ftp_entry *out, int max,
                 char *err, size_t errcap)
{
    char dir[1024];
    localfs_real_path(root, path, dir, sizeof(dir));
    DIR *d = opendir(dir);
    if (!d) {
        int why = errno;
#ifdef PS64_NATIVE
        int n = list_raw(dir, out, max);
        if (n >= 0) {
            ftp_sort(out, n);
            return n;
        }
        printf("localfs: %s: opendir: %s\n", dir, strerror(why));
#endif
        snprintf(err, errcap, why == ENOENT ? "Folder not found: %s" : "Can't open %s (%s)", dir,
                 strerror(why));
        return -1;
    }
    int n = 0;
    struct dirent *de;
    while (n < max && (de = readdir(d)) != NULL)
        add_entry(dir, de->d_name, out, &n);
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
