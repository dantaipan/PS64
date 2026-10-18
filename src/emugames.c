#include "emugames.h"

#include "c64files.h"
#include "emulator.h"
#include "ftp.h"
#include "localfs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define LIST_MAX 1024

void eg_init(emugames *g, const char *games_dir, const char *cache_dir)
{
    memset(g, 0, sizeof(*g));
    snprintf(g->games_dir, sizeof(g->games_dir), "%s", games_dir);
    snprintf(g->cache_dir, sizeof(g->cache_dir), "%s", cache_dir);
}

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* Why a type can't start in the emulator. */
static void cant_start(int type, const char *name, char *msg, size_t cap)
{
    if (type == FT_SID || type == FT_MOD)
        snprintf(msg, cap, "%s: SID and MOD music plays on a C64 Ultimate only", name);
    else
        snprintf(msg, cap, "Can't start %s in the emulator", name);
}

/*
 * Start real paths: one file directly, several disks through an .m3u list
 * (the first one starts). name is for the message.
 */
static int start_files(emugames *g, const char *const *paths, int n, int action, const char *name,
                       char *msg, size_t cap)
{
    if (action == FA_MOUNT_A) {
        if (emulator_insert_disk(paths[0]) != 0) {
            snprintf(msg, cap, "Couldn't insert %s", name);
            return EG_ERROR;
        }
        snprintf(msg, cap, "Inserted %s into drive 8", name);
        return EG_STARTED;
    }
    const char *start = paths[0];
    char m3u[600];
    if (n > 1) {
        snprintf(m3u, sizeof(m3u), "%s/disks.m3u", g->cache_dir);
        if (localfs_mkdirs(g->cache_dir) != 0 || localfs_write_m3u(m3u, paths, n) != 0) {
            snprintf(msg, cap, "Can't write %s", m3u);
            return EG_ERROR;
        }
        start = m3u;
    }
    if (emulator_load(start) != 0) {
        snprintf(msg, cap, "The emulator couldn't start %s", name);
        return EG_ERROR;
    }
    if (n > 1)
        snprintf(msg, cap, "Starting %s (disk 1 of %d)", name, n);
    else
        snprintf(msg, cap, "Starting %s", name);
    return EG_STARTED;
}

int eg_launch_path(emugames *g, const char *path, int action, char *msg, size_t cap)
{
    char file[600];
    snprintf(file, sizeof(file), "%s", path);
    ftp_entry *list = malloc(sizeof(ftp_entry) * LIST_MAX);
    const char **names = malloc(sizeof(char *) * LIST_MAX);
    char err[160];
    int rc = EG_ERROR;
    if (!list || !names) {
        snprintf(msg, cap, "Out of memory");
        goto out;
    }

    if (action == FA_LAUNCH_FOLDER) {
        int n = localfs_list(g->games_dir, path, list, LIST_MAX, err, sizeof(err));
        if (n < 0) {
            snprintf(msg, cap, "%s", err);
            goto out;
        }
        int nn = 0;
        for (int i = 0; i < n; i++)
            if (!list[i].is_dir)
                names[nn++] = list[i].name;
        int pick = files_pick_launch(names, nn);
        if (pick < 0) {
            snprintf(msg, cap, "Nothing to start in %s", base_name(path));
            goto out;
        }
        snprintf(file, sizeof(file), "%s%s%s", path, strcmp(path, "/") ? "/" : "", names[pick]);
        action = FA_NONE;
    }

    int type = files_type(file, 0);
    if (action == FA_NONE)
        action = files_default_action(type);
    if (action == FA_NONE) {
        cant_start(type, base_name(file), msg, cap);
        goto out;
    }

    /* A disk: take the other disks in its folder along, for swapping. */
    char real[EG_MAX_DISKS][600];
    const char *paths[EG_MAX_DISKS];
    int count = 1;
    localfs_real_path(g->games_dir, file, real[0], sizeof(real[0]));
    paths[0] = real[0];
    if (type == FT_DISK && action != FA_MOUNT_A) {
        char folder[600];
        snprintf(folder, sizeof(folder), "%s", file);
        char *slash = strrchr(folder, '/');
        if (slash)
            slash[slash == folder ? 1 : 0] = 0;
        int n = localfs_list(g->games_dir, folder, list, LIST_MAX, err, sizeof(err));
        int nn = 0, chosen = -1;
        for (int i = 0; i < n; i++)
            if (!list[i].is_dir) {
                if (!strcmp(list[i].name, base_name(file)))
                    chosen = nn;
                names[nn++] = list[i].name;
            }
        int order[EG_MAX_DISKS];
        int nd = localfs_disk_set(names, nn, chosen, order, EG_MAX_DISKS);
        if (nd > 1) {
            count = nd;
            for (int i = 0; i < nd; i++) {
                char p[600];
                snprintf(p, sizeof(p), "%s%s%s", folder, strcmp(folder, "/") ? "/" : "", names[order[i]]);
                localfs_real_path(g->games_dir, p, real[i], sizeof(real[i]));
                paths[i] = real[i];
            }
        }
    }
    rc = start_files(g, paths, count, action, base_name(file), msg, cap);
out:
    free(list);
    free(names);
    return rc;
}

/* ---- Assembly64 ------------------------------------------------------------------ */

static void cache_path(const emugames *g, const char *item_id, const char *name, char *out, size_t cap)
{
    char item[64], file[160];
    localfs_safe_name(item_id, item, sizeof(item));
    localfs_safe_name(name, file, sizeof(file));
    snprintf(out, cap, "%s/downloads/%s/%s", g->cache_dir, item, file);
}

static int cached(const char *path, unsigned long long size)
{
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0 && (size == 0 || (unsigned long long)st.st_size == size);
}

/* Fetch the next file that isn't cached, or start when all are there. */
static int advance(emugames *g, a64 *a, char *msg, size_t cap)
{
    while (g->next < g->nfiles) {
        const a64_file *f = &g->files[g->next];
        cache_path(g, g->item_id, f->path, g->saved[g->next], sizeof(g->saved[0]));
        if (!cached(g->saved[g->next], f->size)) {
            a64_download(a, g->item_id, g->category, f->id, f->path, g->action);
            if (g->nfiles > 1)
                snprintf(msg, cap, "Downloading %s (%d of %d)...", f->path, g->next + 1, g->nfiles);
            else
                snprintf(msg, cap, "Downloading %s...", f->path);
            return EG_WAITING;
        }
        g->next++;
    }
    g->active = 0;
    const char *paths[EG_MAX_DISKS];
    for (int i = 0; i < g->nfiles; i++)
        paths[i] = g->saved[i];
    const char *name = g->item_name[0] ? g->item_name : g->files[0].path;
    return start_files(g, paths, g->nfiles, g->action, name, msg, cap);
}

int eg_a64_begin(emugames *g, a64 *a, const char *item_id, int category, const char *item_name,
                 const a64_file *files, int nfiles, int chosen, int action, char *msg, size_t cap)
{
    if (chosen < 0 || chosen >= nfiles) {
        snprintf(msg, cap, "Nothing to start");
        return EG_ERROR;
    }
    int type = files_type(files[chosen].path, 0);
    if (action == FA_NONE || action == FA_MOUNT_RUN || action == FA_RUN || action == FA_RUN_CRT ||
        action == FA_LOAD || action == FA_MOUNT_B)
        action = action == FA_MOUNT_B ? FA_MOUNT_A : files_default_action(type);
    if (action == FA_NONE) {
        cant_start(type, files[chosen].path, msg, cap);
        return EG_ERROR;
    }
    g->active = 1;
    snprintf(g->item_id, sizeof(g->item_id), "%s", item_id);
    snprintf(g->item_name, sizeof(g->item_name), "%s", item_name ? item_name : "");
    g->category = category;
    g->action = action;
    g->next = 0;
    g->files[0] = files[chosen];
    g->nfiles = 1;
    if (type == FT_DISK && action != FA_MOUNT_A) {
        const char *names[256];
        int nn = nfiles < 256 ? nfiles : 256, order[EG_MAX_DISKS];
        for (int i = 0; i < nn; i++)
            names[i] = files[i].path;
        int nd = localfs_disk_set(names, nn, chosen, order, EG_MAX_DISKS);
        if (nd > 1) {
            for (int i = 0; i < nd; i++)
                g->files[i] = files[order[i]];
            g->nfiles = nd;
        }
    }
    return advance(g, a, msg, cap);
}

int eg_a64_file(emugames *g, a64 *a, const a64_result *r, char *msg, size_t cap)
{
    msg[0] = 0;
    if (!g->active || g->next >= g->nfiles)
        return EG_ERROR;
    if (!r->data || localfs_write(g->saved[g->next], r->data, r->len) != 0) {
        g->active = 0;
        snprintf(msg, cap, "Couldn't save %s", r->name);
        return EG_ERROR;
    }
    g->next++;
    return advance(g, a, msg, cap);
}

void eg_a64_cancel(emugames *g)
{
    g->active = 0;
}
