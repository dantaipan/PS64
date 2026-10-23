#include "sidrandom.h"

#include "c64files.h"
#include "localfs.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define FOLDER_MAX 4096           /* entries read from one folder */

struct sidrandom {
    char            root[600];
    unsigned        seed;
    pthread_t       thread;
    pthread_mutex_t lock;
    volatile int    stop;
    int             done, found;
    char            path[512];
};

int sidrandom_take(unsigned *state, int n)
{
    /* xorshift32 */
    unsigned x = *state ? *state : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return n > 0 && x % (unsigned)n == 0;
}

/* "/USB0".."/USB7" at the top: the drives localfs adds, not the SID folder's own. */
static int is_usb(const char *path, const ftp_entry *e)
{
    return !strcmp(path, "/") && e->is_dir && !strncasecmp(e->name, "USB", 3) &&
           e->name[3] >= '0' && e->name[3] < '0' + LOCALFS_USB_DRIVES && !e->name[4];
}

int sidrandom_pick(const char *root, unsigned seed, int max_dirs, const volatile int *stop,
                   char *out, size_t cap)
{
    ftp_entry *e = malloc(sizeof(*e) * FOLDER_MAX);
    int cap_dirs = 256, ndirs = 0, seen = 0, visited = 0;
    char **dirs = malloc(sizeof(*dirs) * (size_t)cap_dirs);   /* folders still to look in */
    if (!e || !dirs) {
        free(e);
        free(dirs);
        return 0;
    }
    dirs[ndirs++] = strdup("/");
    unsigned state = seed ? seed : 1;
    while (ndirs > 0 && visited < max_dirs && !(stop && *stop)) {
        char *path = dirs[--ndirs];
        visited++;
        char err[160];
        int n = path ? localfs_list(root, path, e, FOLDER_MAX, err, sizeof(err)) : -1;
        for (int i = 0; i < n; i++) {
            char child[512];
            int len = snprintf(child, sizeof(child), "%s/%s", strcmp(path, "/") ? path : "", e[i].name);
            if (len < 0 || (size_t)len >= sizeof(child))
                continue;
            if (e[i].is_dir) {
                if (is_usb(path, &e[i]))
                    continue;
                if (ndirs == cap_dirs) {
                    char **more = realloc(dirs, sizeof(*dirs) * (size_t)cap_dirs * 2);
                    if (!more)
                        continue;
                    dirs = more;
                    cap_dirs *= 2;
                }
                dirs[ndirs++] = strdup(child);
            } else if (files_type(e[i].name, 0) == FT_SID && sidrandom_take(&state, ++seen)) {
                snprintf(out, cap, "%s", child);
            }
        }
        free(path);
    }
    while (ndirs > 0)
        free(dirs[--ndirs]);
    free(dirs);
    free(e);
    return seen > 0;
}

static void *walk(void *arg)
{
    sidrandom *r = arg;
    char path[512] = "";
    int found = sidrandom_pick(r->root, r->seed, SIDRANDOM_MAX_DIRS, &r->stop, path, sizeof(path));
    pthread_mutex_lock(&r->lock);
    snprintf(r->path, sizeof(r->path), "%s", path);
    r->found = found;
    r->done = 1;
    pthread_mutex_unlock(&r->lock);
    return NULL;
}

sidrandom *sidrandom_start(const char *root, unsigned seed)
{
    sidrandom *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    snprintf(r->root, sizeof(r->root), "%s", root);
    r->seed = seed;
    pthread_mutex_init(&r->lock, NULL);
    if (pthread_create(&r->thread, NULL, walk, r) != 0) {
        pthread_mutex_destroy(&r->lock);
        free(r);
        return NULL;
    }
    return r;
}

int sidrandom_poll(sidrandom *r, char *path, size_t cap)
{
    pthread_mutex_lock(&r->lock);
    int rc = !r->done ? 0 : r->found ? 1 : -1;
    if (rc == 1)
        snprintf(path, cap, "%s", r->path);
    pthread_mutex_unlock(&r->lock);
    return rc;
}

void sidrandom_free(sidrandom *r)
{
    if (!r)
        return;
    r->stop = 1;
    pthread_join(r->thread, NULL);
    pthread_mutex_destroy(&r->lock);
    free(r);
}
