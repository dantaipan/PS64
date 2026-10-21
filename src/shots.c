#include "shots.h"

#include "c64api.h"
#include "image.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE 16
#define CSDB_HOST "csdb.dk"

typedef struct {
    char      id[64];
    uint32_t *pixels;           /* NULL: no screenshot; else w x h*frames */
    int       w, h, frames;
    int      *delays;           /* ms per frame */
    unsigned  used;             /* for least-recently-used replacement */
} entry;

struct shots {
    pthread_t       thread;
    pthread_mutex_t lock;
    pthread_cond_t  wake;
    int             quit;
    char            want[64];   /* id to fetch next, "" if none */
    entry           cache[CACHE];
    int             count;
    unsigned        clock;
};

int shots_url(const char *id, char *out, size_t cap)
{
    if (!id || !*id || strlen(id) > 12)
        return -1;
    for (const char *p = id; *p; p++)
        if (*p < '0' || *p > '9')
            return -1;
    long n = strtol(id, NULL, 10);
    snprintf(out, cap, "/gfx/releases/%ld/%ld.gif", n / 1000 * 1000, n);
    return 0;
}

/* (lock held) */
static entry *find(shots *s, const char *id)
{
    for (int i = 0; i < s->count; i++)
        if (!strcmp(s->cache[i].id, id)) {
            s->cache[i].used = ++s->clock;
            return &s->cache[i];
        }
    return NULL;
}

/* (lock held) Store a result, replacing the least recently used entry when full. */
static void store(shots *s, const char *id, uint32_t *pixels, int w, int h,
                  int frames, int *delays)
{
    entry *e = NULL;
    if (s->count < CACHE) {
        e = &s->cache[s->count++];
    } else {
        e = &s->cache[0];
        for (int i = 1; i < CACHE; i++)
            if (s->cache[i].used < e->used)
                e = &s->cache[i];
        free(e->pixels);
        free(e->delays);
    }
    snprintf(e->id, sizeof(e->id), "%s", id);
    e->pixels = pixels;
    e->w = w;
    e->h = h;
    e->frames = frames;
    e->delays = delays;
    e->used = ++s->clock;
}

static void *worker(void *arg)
{
    shots *s = arg;
    pthread_mutex_lock(&s->lock);
    while (!s->quit) {
        if (!s->want[0] || find(s, s->want)) {
            s->want[0] = 0;
            pthread_cond_wait(&s->wake, &s->lock);
            continue;
        }
        char id[64], path[96];
        snprintf(id, sizeof(id), "%s", s->want);
        pthread_mutex_unlock(&s->lock);

        uint32_t *pixels = NULL;
        int w = 0, h = 0, frames = 0, *delays = NULL;
        if (shots_url(id, path, sizeof(path)) == 0) {
            c64api api = { .host = CSDB_HOST, .timeout_s = 10 };
            char *body = NULL;
            size_t len = 0;
            if (c64api_request_alloc(&api, "GET", path, NULL, NULL, 0, &body, &len, NULL, 0) == 200)
                image_decode(body, len, &pixels, &w, &h, &frames, &delays);
            free(body);
        }

        pthread_mutex_lock(&s->lock);
        store(s, id, pixels, w, h, frames, delays);       /* a miss is remembered too: asked once per item */
        if (!strcmp(s->want, id))
            s->want[0] = 0;
    }
    pthread_mutex_unlock(&s->lock);
    return NULL;
}

shots *shots_start(void)
{
    shots *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init(&s->wake, NULL);
    if (pthread_create(&s->thread, NULL, worker, s) != 0) {
        pthread_mutex_destroy(&s->lock);
        pthread_cond_destroy(&s->wake);
        free(s);
        return NULL;
    }
    return s;
}

void shots_stop(shots *s)
{
    if (!s)
        return;
    pthread_mutex_lock(&s->lock);
    s->quit = 1;
    pthread_cond_signal(&s->wake);
    pthread_mutex_unlock(&s->lock);
    pthread_join(s->thread, NULL);
    for (int i = 0; i < s->count; i++)
    {
        free(s->cache[i].pixels);
        free(s->cache[i].delays);
    }
    pthread_mutex_destroy(&s->lock);
    pthread_cond_destroy(&s->wake);
    free(s);
}

void shots_want(shots *s, const char *id)
{
    if (!s || !id)
        return;
    pthread_mutex_lock(&s->lock);
    snprintf(s->want, sizeof(s->want), "%s", id);
    pthread_cond_signal(&s->wake);
    pthread_mutex_unlock(&s->lock);
}

int shots_fetch(shots *s, const char *id, uint32_t **pixels, int *w, int *h,
                int *frames, int **delays)
{
    if (!s || !id)
        return -1;
    pthread_mutex_lock(&s->lock);
    entry *e = find(s, id);
    int rc = 0;
    if (e && !e->pixels) {
        rc = -1;
    } else if (e) {
        size_t bytes = (size_t)e->w * (size_t)e->h * (size_t)e->frames * sizeof(uint32_t);
        size_t dbytes = (size_t)e->frames * sizeof(int);
        *pixels = malloc(bytes);
        *delays = malloc(dbytes);
        if (*pixels && *delays) {
            memcpy(*pixels, e->pixels, bytes);
            memcpy(*delays, e->delays, dbytes);
            *w = e->w;
            *h = e->h;
            *frames = e->frames;
            rc = 1;
        } else {
            free(*pixels);
            free(*delays);
            rc = -1;
        }
    }
    pthread_mutex_unlock(&s->lock);
    return rc;
}
