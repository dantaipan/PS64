#include "c64index.h"

#include "c64files.h"
#include "ftp.h"
#include "localfs.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define LIST_MAX 4096

struct c64index {
    pthread_mutex_t lock;

    /* Entries: paths in one string pool. */
    char               *pool;
    size_t              pool_len, pool_cap;
    size_t             *offs;
    unsigned long long *sizes;
    int                 count, cap;

    /* Builder. */
    pthread_t    thread;
    int          thread_running;
    int          stop;
    index_status status;
    char         host[64], password[64], roots[256], file[512];
    char         local_root[512];       /* set: index this folder instead of FTP */
};

c64index *index_new(void)
{
    c64index *x = calloc(1, sizeof(*x));
    if (x)
        pthread_mutex_init(&x->lock, NULL);
    return x;
}

static void clear_entries(c64index *x)
{
    free(x->pool);
    free(x->offs);
    free(x->sizes);
    x->pool = NULL;
    x->offs = NULL;
    x->sizes = NULL;
    x->pool_len = x->pool_cap = 0;
    x->count = x->cap = 0;
}

static void join_builder(c64index *x)
{
    if (!x->thread_running)
        return;
    pthread_mutex_lock(&x->lock);
    x->stop = 1;
    pthread_mutex_unlock(&x->lock);
    pthread_join(x->thread, NULL);
    x->thread_running = 0;
}

void index_free(c64index *x)
{
    if (!x)
        return;
    join_builder(x);
    clear_entries(x);
    pthread_mutex_destroy(&x->lock);
    free(x);
}

/* Caller holds the lock (or owns x exclusively). */
static int add_locked(c64index *x, const char *path, unsigned long long size)
{
    if (x->count >= INDEX_MAX_FILES)
        return -1;
    size_t len = strlen(path) + 1;
    if (x->pool_len + len > x->pool_cap) {
        size_t cap = x->pool_cap ? x->pool_cap * 2 : 64 * 1024;
        while (cap < x->pool_len + len)
            cap *= 2;
        char *p = realloc(x->pool, cap);
        if (!p)
            return -1;
        x->pool = p;
        x->pool_cap = cap;
    }
    if (x->count == x->cap) {
        int cap = x->cap ? x->cap * 2 : 1024;
        size_t *o = realloc(x->offs, sizeof(*o) * (size_t)cap);
        if (!o)
            return -1;
        x->offs = o;
        unsigned long long *s = realloc(x->sizes, sizeof(*s) * (size_t)cap);
        if (!s)
            return -1;
        x->sizes = s;
        x->cap = cap;
    }
    memcpy(x->pool + x->pool_len, path, len);
    x->offs[x->count] = x->pool_len;
    x->sizes[x->count] = size;
    x->pool_len += len;
    x->count++;
    return 0;
}

int index_add(c64index *x, const char *path, unsigned long long size)
{
    pthread_mutex_lock(&x->lock);
    int rc = add_locked(x, path, size);
    pthread_mutex_unlock(&x->lock);
    return rc;
}

int index_count(c64index *x)
{
    pthread_mutex_lock(&x->lock);
    int n = x->count;
    pthread_mutex_unlock(&x->lock);
    return n;
}

/* ---- save / load -------------------------------------------------------------- */

int index_save(c64index *x, const char *file, const char *host, const char *roots)
{
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", file);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return -1;
    pthread_mutex_lock(&x->lock);
    fprintf(f, "# c64view index v1\thost=%s\troots=%s\tdate=%ld\n", host, roots, (long)time(NULL));
    for (int i = 0; i < x->count; i++)
        fprintf(f, "%llu\t%s\n", x->sizes[i], x->pool + x->offs[i]);
    pthread_mutex_unlock(&x->lock);
    if (fclose(f) != 0 || rename(tmp, file) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

int index_load(c64index *x, const char *file, const char *host)
{
    FILE *f = fopen(file, "r");
    if (!f)
        return -1;
    char line[600];
    char want[128];
    snprintf(want, sizeof(want), "\thost=%s\t", host);
    if (!fgets(line, sizeof(line), f) || strncmp(line, "# c64view index v1", 18) != 0 ||
        !strstr(line, want)) {
        fclose(f);
        return -1;
    }
    pthread_mutex_lock(&x->lock);
    clear_entries(x);
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        char *tab = strchr(line, '\t');
        if (!tab)
            continue;
        *tab = 0;
        add_locked(x, tab + 1, strtoull(line, NULL, 10));
    }
    pthread_mutex_unlock(&x->lock);
    fclose(f);
    return 0;
}

/* ---- builder ---------------------------------------------------------------------- */

typedef struct {
    char **items;
    int    head, len, cap;
} dirq;

static void q_push(dirq *q, const char *path)
{
    if (q->head + q->len == q->cap) {
        if (q->head > 0) {
            memmove(q->items, q->items + q->head, sizeof(char *) * (size_t)q->len);
            q->head = 0;
        } else {
            q->cap = q->cap ? q->cap * 2 : 256;
            q->items = realloc(q->items, sizeof(char *) * (size_t)q->cap);
        }
    }
    q->items[q->head + q->len++] = strdup(path);
}

static char *q_pop(dirq *q)
{
    if (!q->len)
        return NULL;
    q->len--;
    return q->items[q->head++];
}

static int depth_of(const char *path)
{
    int d = 0;
    for (const char *p = path; *p; p++)
        d += *p == '/';
    return d;
}

static int stopping(c64index *x)
{
    pthread_mutex_lock(&x->lock);
    int s = x->stop;
    pthread_mutex_unlock(&x->lock);
    return s;
}

static void *builder(void *arg)
{
    c64index *x = arg;
    const char *pw = x->password[0] ? x->password : NULL;
    ftp_entry *list = malloc(sizeof(ftp_entry) * LIST_MAX);
    dirq q = {0};
    char err[128] = "";

    char roots[256];
    snprintf(roots, sizeof(roots), "%s", x->roots);
    char *save = NULL;
    for (char *r = strtok_r(roots, ",", &save); r; r = strtok_r(NULL, ",", &save)) {
        while (*r == ' ') r++;
        if (*r)
            q_push(&q, r);
    }

    int local = x->local_root[0] != 0;
    if (local)      /* a native PS5 app can't list folders: refresh games.txt over FTP first */
        localfs_refresh_manifest(x->local_root, err, sizeof(err));
    ftp_session *s = local ? NULL : ftp_open(x->host, pw, err, sizeof(err));
    int fatal = !local && s == NULL;
    char *dir;
    while (!fatal && (dir = q_pop(&q)) != NULL) {
        if (stopping(x)) {
            free(dir);
            break;
        }
        pthread_mutex_lock(&x->lock);
        snprintf(x->status.current, sizeof(x->status.current), "%s", dir);
        pthread_mutex_unlock(&x->lock);

        int n = !list ? -1 : local ? localfs_list(x->local_root, dir, list, LIST_MAX, err, sizeof(err))
                                   : ftp_list_dir(s, dir, list, LIST_MAX, err, sizeof(err));
        if (n < 0 && !local) {
            /* Reconnect once if the connection dropped, then retry this folder. */
            ftp_close(s);
            s = ftp_open(x->host, pw, err, sizeof(err));
            n = s && list ? ftp_list_dir(s, dir, list, LIST_MAX, err, sizeof(err)) : -1;
            if (!s)
                fatal = 1;
        }

        pthread_mutex_lock(&x->lock);
        if (n < 0) {
            x->status.failed_dirs++;
        } else {
            x->status.dirs++;
            for (int i = 0; i < n; i++) {
                char path[512];
                snprintf(path, sizeof(path), "%s%s%s", dir, strcmp(dir, "/") ? "/" : "", list[i].name);
                if (list[i].is_dir) {
                    if (depth_of(path) <= INDEX_MAX_DEPTH)
                        q_push(&q, path);
                    continue;
                }
                int t = files_type(list[i].name, 0);
                /* What the machine can start: no tapes on the Ultimate, no SID/MOD in the emulator. */
                int wanted = t != FT_OTHER && (local ? t != FT_SID && t != FT_MOD : t != FT_TAPE);
                if (wanted && strlen(path) < sizeof(((index_hit *)0)->path) &&
                    add_locked(x, path, list[i].size) == 0)
                    x->status.files++;
            }
        }
        pthread_mutex_unlock(&x->lock);
        free(dir);
    }
    while ((dir = q_pop(&q)) != NULL)
        free(dir);
    free(q.items);
    free(list);
    ftp_close(s);

    int cancelled = stopping(x);
    if (!fatal && !cancelled)
        index_save(x, x->file, x->host, x->roots);

    pthread_mutex_lock(&x->lock);
    x->status.building = 0;
    x->status.done = !fatal && !cancelled;
    x->status.cancelled = cancelled;
    x->status.current[0] = 0;
    if (fatal)
        snprintf(x->status.error, sizeof(x->status.error), "%s", err);
    pthread_mutex_unlock(&x->lock);
    return NULL;
}

void index_build_start(c64index *x, const char *host, const char *password,
                       const char *roots, const char *file)
{
    join_builder(x);
    pthread_mutex_lock(&x->lock);
    clear_entries(x);
    memset(&x->status, 0, sizeof(x->status));
    x->status.building = 1;
    x->stop = 0;
    snprintf(x->host, sizeof(x->host), "%s", host);
    snprintf(x->password, sizeof(x->password), "%s", password ? password : "");
    snprintf(x->roots, sizeof(x->roots), "%s", roots);
    snprintf(x->file, sizeof(x->file), "%s", file);
    x->local_root[0] = 0;
    pthread_mutex_unlock(&x->lock);
    if (pthread_create(&x->thread, NULL, builder, x) == 0) {
        x->thread_running = 1;
    } else {
        pthread_mutex_lock(&x->lock);
        x->status.building = 0;
        snprintf(x->status.error, sizeof(x->status.error), "Couldn't start indexing");
        pthread_mutex_unlock(&x->lock);
    }
}

void index_build_local_start(c64index *x, const char *root, const char *file)
{
    join_builder(x);
    pthread_mutex_lock(&x->lock);
    clear_entries(x);
    memset(&x->status, 0, sizeof(x->status));
    x->status.building = 1;
    x->stop = 0;
    snprintf(x->host, sizeof(x->host), "%s", INDEX_LOCAL_HOST);
    x->password[0] = 0;
    snprintf(x->roots, sizeof(x->roots), "/");
    snprintf(x->file, sizeof(x->file), "%s", file);
    snprintf(x->local_root, sizeof(x->local_root), "%s", root);
    pthread_mutex_unlock(&x->lock);
    if (pthread_create(&x->thread, NULL, builder, x) == 0) {
        x->thread_running = 1;
    } else {
        pthread_mutex_lock(&x->lock);
        x->status.building = 0;
        snprintf(x->status.error, sizeof(x->status.error), "Couldn't start indexing");
        pthread_mutex_unlock(&x->lock);
    }
}

void index_status_get(c64index *x, index_status *st)
{
    pthread_mutex_lock(&x->lock);
    *st = x->status;
    pthread_mutex_unlock(&x->lock);
}

/* ---- search ----------------------------------------------------------------------- */

/* Case-insensitive find of a lower-case needle; returns the match or NULL. */
static const char *find_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (strncasecmp(hay, needle, n) == 0)
            return hay;
    return NULL;
}

/* Score of one word in one string: 0 = absent, 2 = partial, 3 = whole word. */
static int word_score(const char *s, const char *word)
{
    int best = 0;
    size_t n = strlen(word);
    for (const char *p = find_ci(s, word); p; p = find_ci(p + 1, word)) {
        int start = p == s || !isalnum((unsigned char)p[-1]);
        int end = !isalnum((unsigned char)p[n]);
        int sc = start && end ? 3 : 2;
        if (sc > best)
            best = sc;
    }
    return best;
}

/* title = "<folder>/<file>": collections often name the folder after the
 * game and the file generically ("Boulder Dash/Disk1.d64"). */
typedef struct { int idx, score; const char *title, *name; } cand;

static int cand_cmp(const void *a, const void *b)
{
    const cand *x = a, *y = b;
    if (x->score != y->score)
        return y->score - x->score;
    /* Same score: the shorter title is the closer match ("Boulder Dash/..."
     * before "Boulder Dash II/..."). */
    size_t lx = strlen(x->title), ly = strlen(y->title);
    if (lx != ly)
        return lx < ly ? -1 : 1;
    return strcasecmp(x->title, y->title);
}

#define PROGRAM_BONUS 8     /* things you can run rank above music */

int index_search(c64index *x, const char *query, index_hit *out, int max)
{
    char q[128], *words[8];
    int nw = 0;
    snprintf(q, sizeof(q), "%s", query);
    char *save = NULL;
    for (char *w = strtok_r(q, " \t", &save); w && nw < 8; w = strtok_r(NULL, " \t", &save))
        words[nw++] = w;
    if (nw == 0 || max <= 0)
        return 0;

    pthread_mutex_lock(&x->lock);
    cand *c = malloc(sizeof(*c) * (size_t)(x->count ? x->count : 1));
    int nc = 0;
    for (int i = 0; c && i < x->count; i++) {
        const char *path = x->pool + x->offs[i];
        const char *slash = strrchr(path, '/');
        const char *name = slash ? slash + 1 : path;
        const char *title = path;
        for (const char *p = slash ? slash - 1 : NULL; p && p >= path; p--)
            if (*p == '/') {
                title = p + 1;
                break;
            }
        int score = 0, ok = 1;
        for (int w = 0; w < nw && ok; w++) {
            size_t wl = strlen(words[w]);
            int in_title = word_score(title, words[w]);
            if (in_title)
                score += 10 + 2 * in_title +    /* folder/file-name matches rank first, whole words */
                         (!strncasecmp(title, words[w], wl) ||
                          !strncasecmp(name, words[w], wl));    /* names starting with it more */
            else if (word_score(path, words[w]))
                score += 1 + word_score(path, words[w]);
            else
                ok = 0;
        }
        int type = files_type(name, 0);
        if (ok)
            c[nc++] = (cand){ i, score + (type != FT_SID && type != FT_MOD ? PROGRAM_BONUS : 0),
                              title, name };
    }
    if (c)
        qsort(c, (size_t)nc, sizeof(*c), cand_cmp);
    int n = nc < max ? nc : max;
    for (int i = 0; i < n; i++) {
        const char *path = x->pool + x->offs[c[i].idx];
        snprintf(out[i].path, sizeof(out[i].path), "%s", path);
        out[i].size = x->sizes[c[i].idx];
        out[i].type = files_type(c[i].name, 0);
        out[i].score = c[i].score;
    }
    pthread_mutex_unlock(&x->lock);
    free(c);
    return n;
}
