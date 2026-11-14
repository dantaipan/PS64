#include "roms.h"

#include "localfs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROM_MAX 0x80000           /* the largest the SuperCPU's can be (VICE: 512 KB) */

/* The ROMs VICE looks for in system/vice (retrodep/ui.c, the SuperCPU's in its SCPU64). */
static const struct { int rom; const char *file; } targets[] = {
    { ROM_C64,  "JiffyDOS_C64.bin" },
    { ROM_1541, "JiffyDOS_1541-II.bin" },
    { ROM_1571, "JiffyDOS_1571_repl310654.bin" },
    { ROM_1581, "JiffyDOS_1581.bin" },
    { ROM_SCPU, "SCPU64/scpu-dos-2.04.bin" },
};
#define NTARGETS (int)(sizeof(targets) / sizeof(targets[0]))

/* text in data, ignoring case and the top bit (CBM ROMs end their strings with it set). */
static int has(const unsigned char *d, size_t n, const char *text)
{
    size_t m = strlen(text);
    for (size_t i = 0; i + m <= n; i++) {
        size_t j = 0;
        while (j < m && toupper(d[i + j] & 0x7f) == text[j])
            j++;
        if (j == m)
            return 1;
    }
    return 0;
}

int roms_identify(const char *name, const unsigned char *d, size_t n)
{
    if (!d || !has(d, n, "JIFFYDOS"))
        return 0;
    if (n == 0x2000 && has(d, n, "BYTES FREE"))      /* (a drive's says BLOCKS FREE) */
        return ROM_C64;
    if (n == 0x4000 && has(d, n, "1541"))
        return ROM_1541;
    if (n == 0x8000 && has(d, n, "1581"))
        return ROM_1581;
    if (n == 0x8000 && has(d, n, "1571"))
        return ROM_1571;
    if (n >= 0x10000 && n <= ROM_MAX && (has(d, n, "SUPERCPU") || has(d, n, "SUPER CPU") ||
                                         (name && (has((const unsigned char *)name, strlen(name), "SCPU") ||
                                                  has((const unsigned char *)name, strlen(name), "SUPERCPU")))))
        return ROM_SCPU;
    return 0;
}

int roms_jiffydos(int found, int supercpu)
{
    if (supercpu)
        return (found & ROM_SCPU) != 0;
    return (found & ROM_C64) && (found & (ROM_1541 | ROM_1571 | ROM_1581));
}

/* A whole file, if it's no bigger than a ROM. */
static unsigned char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    unsigned char *d = malloc(ROM_MAX + 1);
    size_t n = d ? fread(d, 1, ROM_MAX + 1, f) : 0;
    fclose(f);
    if (!d || n == 0 || n > ROM_MAX) {
        free(d);
        return NULL;
    }
    *len = n;
    return d;
}

/* Look through dir's files (and its folders', depth more levels) for each ROM. */
static void find(const char *root, const char *path, int depth, char found[][800], int *mask)
{
    enum { MAX = 512 };
    ftp_entry *e = malloc(sizeof(*e) * MAX);
    char err[160];
    int n = e ? localfs_list(root, path, e, MAX, err, sizeof(err)) : -1;
    for (int i = 0; i < n; i++) {
        char sub[800];
        snprintf(sub, sizeof(sub), "%s%s%s", path, strcmp(path, "/") ? "/" : "", e[i].name);
        if (e[i].is_dir) {
            if (!strcmp(path, "/") && !strncmp(e[i].name, "USB", 3))
                continue;                        /* (localfs lists the USB drives at the top) */
            if (depth > 0)
                find(root, sub, depth - 1, found, mask);
            continue;
        }
        char real[800];
        localfs_real_path(root, sub, real, sizeof(real));
        size_t len;
        unsigned char *d = read_file(real, &len);
        int rom = roms_identify(e[i].name, d, len);
        free(d);
        for (int t = 0; t < NTARGETS; t++)
            if (rom == targets[t].rom && !(*mask & rom)) {
                snprintf(found[t], sizeof(found[0]), "%s", real);
                *mask |= rom;
            }
    }
    free(e);
}

/* Copy from to to, unless to already has the same bytes. 0 or -1. */
static int copy(const char *from, const char *to)
{
    size_t n, m;
    unsigned char *d = read_file(from, &n), *old = read_file(to, &m);
    int same = d && old && n == m && !memcmp(d, old, n);
    int rc = !d ? -1 : same ? 0 : localfs_write(to, d, n);
    free(d);
    free(old);
    return rc;
}

int roms_install(const char *dir, const char *vice_dir)
{
    char found[NTARGETS][800];
    int mask = 0;
    memset(found, 0, sizeof(found));
    find(dir, "/", 2, found, &mask);
    for (int t = 0; t < NTARGETS; t++) {
        char to[800];
        snprintf(to, sizeof(to), "%s/%s", vice_dir, targets[t].file);
        if (!found[t][0])
            remove(to);                          /* (taken out of the roms folder) */
        else if (copy(found[t], to) != 0) {
            fprintf(stderr, "roms: can't copy %s to %s\n", found[t], to);
            mask &= ~targets[t].rom;
        }
    }
    return mask;
}
