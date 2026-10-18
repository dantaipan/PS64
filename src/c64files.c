#include "c64files.h"

#include "c64api.h"
#include "c64keys.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define MAX_ENTRIES 2048

/* ---- file types ------------------------------------------------------------ */

static const char *ext_of(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot ? dot + 1 : "";
}

int files_type(const char *name, int is_dir)
{
    if (is_dir)
        return FT_DIR;
    const char *e = ext_of(name);
    if (!strcasecmp(e, "prg"))
        return FT_PRG;
    if (!strcasecmp(e, "d64") || !strcasecmp(e, "d71") || !strcasecmp(e, "d81") ||
        !strcasecmp(e, "g64") || !strcasecmp(e, "g71"))
        return FT_DISK;
    if (!strcasecmp(e, "crt"))
        return FT_CRT;
    if (!strcasecmp(e, "sid"))
        return FT_SID;
    if (!strcasecmp(e, "mod"))
        return FT_MOD;
    return FT_OTHER;
}

int files_default_action(int type)
{
    switch (type) {
    case FT_PRG:  return FA_RUN;
    case FT_DISK: return FA_MOUNT_RUN;
    case FT_CRT:  return FA_RUN_CRT;
    case FT_SID:
    case FT_MOD:  return FA_PLAY;
    }
    return FA_NONE;
}

int files_actions_for(int type, int *out, int max)
{
    static const int prg[] = { FA_RUN, FA_LOAD };
    static const int disk[] = { FA_MOUNT_RUN, FA_MOUNT_A, FA_MOUNT_B };
    static const int crt[] = { FA_RUN_CRT };
    static const int play[] = { FA_PLAY };
    const int *src = NULL;
    int n = 0;
    switch (type) {
    case FT_PRG:  src = prg;  n = 2; break;
    case FT_DISK: src = disk; n = 3; break;
    case FT_CRT:  src = crt;  n = 1; break;
    case FT_SID:
    case FT_MOD:  src = play; n = 1; break;
    }
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        out[i] = src[i];
    return n;
}

const char *files_action_name(int action)
{
    static const char *const names[FA_COUNT] = {
        "Run", "Load", "Mount & run", "Mount on drive A", "Mount on drive B", "Run cartridge", "Play",
        "Reset", "Reboot", "Launch",
    };
    return action >= 0 && action < FA_COUNT ? names[action] : "";
}

const char *files_type_tag(int type)
{
    static const char *const tags[] = { "DIR", "PRG", "DISK", "CRT", "SID", "MOD", "" };
    return tags[type];
}

int files_pick_launch(const char *const *names, int n)
{
    static const int order[] = { FT_DISK, FT_PRG, FT_CRT, FT_SID, FT_MOD };
    for (size_t o = 0; o < sizeof(order) / sizeof(order[0]); o++) {
        int best = -1;
        for (int i = 0; i < n; i++)
            if (files_type(names[i], 0) == order[o] &&
                (best < 0 || strcasecmp(names[i], names[best]) < 0))
                best = i;
        if (best >= 0)
            return best;
    }
    return -1;
}

int files_screen_ready(const unsigned char *screen, int len)
{
    static const unsigned char ready[6] = { 0x12, 0x05, 0x01, 0x04, 0x19, 0x2E };  /* READY. */
    for (int i = 0; i + 6 <= len; i++)
        if (!memcmp(screen + i, ready, 6))
            return 1;
    return 0;
}

/* ---- worker ------------------------------------------------------------------ */

enum { REQ_NONE, REQ_LIST, REQ_ACTION, REQ_DATA };

struct c64files {
    char            host[64], password[64];
    c64api          api;
    remote         *rem;
    pthread_t       thread;
    pthread_mutex_t lock;
    pthread_cond_t  wake;
    int             quit;
    int             req, req_action, busy;
    char            req_path[512];
    unsigned char  *req_data;           /* REQ_DATA: owned until the worker takes it */
    size_t          req_len;
    files_result    result;
    int             have_result;
};

static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void type_text(remote *rem, const char *text)
{
    for (const char *p = text; *p; p++) {
        int key, mods;
        if (c64key_from_ascii(*p, &key, &mods) == 0)
            remote_key_tap(rem, key, mods);
    }
}

static int rest_put(c64files *f, const char *route, const char *param, const char *path)
{
    char enc[1024], req[1200];
    c64api_url_encode(path, enc, sizeof(enc));
    snprintf(req, sizeof(req), "%s?%s=%s", route, param, enc);
    return c64api_request(&f->api, "PUT", req, NULL, NULL, 0, NULL, 0, NULL);
}

/*
 * Reset or reboot (route), then wait until BASIC shows READY. Screen memory
 * is blanked first so an old READY. on screen can't count. Returns the HTTP
 * status, or -2 if READY. didn't appear within 10 s.
 */
static int restart_and_wait(c64files *f, const char *route)
{
    unsigned char spaces[1000];
    memset(spaces, 0x20, sizeof(spaces));
    c64api_request(&f->api, "POST", "/v1/machine:writemem?address=0400", "application/octet-stream",
                   spaces, sizeof(spaces), NULL, 0, NULL);
    int status = c64api_request(&f->api, "PUT", route, NULL, NULL, 0, NULL, 0, NULL);
    if (status < 200 || status >= 300)
        return status;
    for (int tries = 0; tries < 50; tries++) {
        sleep_ms(200);
        unsigned char screen[1000];
        size_t got = 0;
        if (c64api_request(&f->api, "GET", "/v1/machine:readmem?address=0400&length=1000",
                           NULL, NULL, 0, screen, sizeof(screen), &got) == 200 &&
            files_screen_ready(screen, (int)got))
            return status;
    }
    return -2;
}

static int post_data(c64files *f, const char *route, const unsigned char *data, size_t len)
{
    return c64api_request(&f->api, "POST", route, "application/octet-stream", data, len,
                          NULL, 0, NULL);
}

/* Actions on a file sent along with the request (e.g. an Assembly64 download). */
static int do_data_action(c64files *f, const char *name, const unsigned char *data, size_t len,
                          int action, char *msg, size_t cap)
{
    const char *dot = strrchr(name, '.');
    char type[8] = "d64", route[64];
    if (dot && strlen(dot + 1) < sizeof(type))
        for (size_t i = 0; i <= strlen(dot + 1); i++)
            type[i] = (char)tolower((unsigned char)dot[1 + i]);
    int status;
    switch (action) {
    case FA_RUN:     status = post_data(f, "/v1/runners:run_prg", data, len); break;
    case FA_LOAD:    status = post_data(f, "/v1/runners:load_prg", data, len); break;
    case FA_RUN_CRT: status = post_data(f, "/v1/runners:run_crt", data, len); break;
    case FA_PLAY:
        status = post_data(f, files_type(name, 0) == FT_MOD ? "/v1/runners:modplay"
                                                            : "/v1/runners:sidplay", data, len);
        break;
    case FA_MOUNT_A:
    case FA_MOUNT_B:
        snprintf(route, sizeof(route), "/v1/drives/%c:mount?type=%s",
                 action == FA_MOUNT_A ? 'a' : 'b', type);
        status = post_data(f, route, data, len);
        break;
    case FA_MOUNT_RUN:
        status = restart_and_wait(f, "/v1/machine:reboot");
        if (status == -2) {
            snprintf(msg, cap, "The C64 didn't reach READY. after rebooting");
            return -1;
        }
        if (status < 200 || status >= 300)
            break;
        snprintf(route, sizeof(route), "/v1/drives/a:mount?type=%s", type);
        status = post_data(f, route, data, len);
        if (status >= 200 && status < 300 && f->rem)
            type_text(f->rem, "LOAD\"*\",8,1\nRUN\n");
        break;
    default:
        snprintf(msg, cap, "Nothing to do for %s", name);
        return -1;
    }
    if (status < 200 || status >= 300) {
        snprintf(msg, cap, status < 0 ? "Can't reach the C64" : "The C64 refused that (HTTP %d)",
                 status);
        return -1;
    }
    snprintf(msg, cap, "%s %s", action == FA_MOUNT_RUN ? "Loading" :
             action == FA_PLAY ? "Playing" : action == FA_MOUNT_A || action == FA_MOUNT_B ? "Mounted"
             : "Started", name);
    return 0;
}

static int do_action(c64files *f, const char *path, int action, char *msg, size_t cap)
{
    /* "<folder>/<file>" for messages: collections name the folder after the
     * game and the file generically (Boulder Dash/Disk1.d64). */
    const char *name = path;
    const char *last = strrchr(path, '/');
    for (const char *p = last ? last - 1 : NULL; p && p >= path; p--)
        if (*p == '/') {
            name = p + 1;
            break;
        }
    int status;
    switch (action) {
    case FA_RUN:
        status = rest_put(f, "/v1/runners:run_prg", "file", path);
        snprintf(msg, cap, "Running %s", name);
        break;
    case FA_LOAD:
        status = rest_put(f, "/v1/runners:load_prg", "file", path);
        snprintf(msg, cap, "Loaded %s", name);
        break;
    case FA_RUN_CRT:
        status = rest_put(f, "/v1/runners:run_crt", "file", path);
        snprintf(msg, cap, "Starting %s", name);
        break;
    case FA_PLAY:
        status = rest_put(f, files_type(path, 0) == FT_MOD ? "/v1/runners:modplay"
                                                           : "/v1/runners:sidplay", "file", path);
        snprintf(msg, cap, "Playing %s", name);
        break;
    case FA_MOUNT_A:
    case FA_MOUNT_B:
        status = rest_put(f, action == FA_MOUNT_A ? "/v1/drives/a:mount" : "/v1/drives/b:mount",
                          "image", path);
        snprintf(msg, cap, "Mounted %s on drive %c", name, action == FA_MOUNT_A ? 'A' : 'B');
        break;
    case FA_MOUNT_RUN: {
        /* Reboot first: mounting while a game runs can crash it, and a plain
         * reset would restart a cartridge game instead of reaching BASIC. */
        status = restart_and_wait(f, "/v1/machine:reboot");
        if (status == -2) {
            snprintf(msg, cap, "The C64 didn't reach READY. after rebooting");
            return -1;
        }
        if (status < 200 || status >= 300)
            break;
        status = rest_put(f, "/v1/drives/a:mount", "image", path);
        if (status < 200 || status >= 300)
            break;
        if (f->rem)
            type_text(f->rem, "LOAD\"*\",8,1\nRUN\n");
        snprintf(msg, cap, "Mounted %s - loading", name);
        return 0;
    }
    case FA_LAUNCH_FOLDER: {
        /* A game folder: start its first runnable file. */
        ftp_entry *list = malloc(sizeof(ftp_entry) * 256);
        int count = list ? ftp_list(f->host, f->password[0] ? f->password : NULL, path, list, 256,
                                    msg, cap) : -1;
        if (count < 0) {
            free(list);
            return -1;
        }
        const char *names[256];
        int nn = 0, map[256];
        for (int i = 0; i < count; i++)
            if (!list[i].is_dir) {
                map[nn] = i;
                names[nn++] = list[i].name;
            }
        int pick = files_pick_launch(names, nn);
        if (pick < 0) {
            free(list);
            snprintf(msg, cap, "Nothing to run in %s", name);
            return -1;
        }
        char child[600];
        snprintf(child, sizeof(child), "%s/%s", path, list[map[pick]].name);
        free(list);
        return do_action(f, child, files_default_action(files_type(child, 0)), msg, cap);
    }
    case FA_RESET:
    case FA_REBOOT:
        status = c64api_request(&f->api, "PUT", action == FA_RESET ? "/v1/machine:reset"
                                                                    : "/v1/machine:reboot",
                                NULL, NULL, 0, NULL, 0, NULL);
        snprintf(msg, cap, action == FA_RESET ? "C64 reset" : "C64 rebooted");
        break;
    default:
        snprintf(msg, cap, "Nothing to do for %s", name);
        return -1;
    }
    if (status < 200 || status >= 300) {
        snprintf(msg, cap, status < 0 ? "Can't reach the C64" : "The C64 refused that (HTTP %d)",
                 status);
        return -1;
    }
    return 0;
}

static void *worker(void *arg)
{
    c64files *f = arg;
    for (;;) {
        pthread_mutex_lock(&f->lock);
        while (!f->quit && f->req == REQ_NONE)
            pthread_cond_wait(&f->wake, &f->lock);
        if (f->quit) {
            pthread_mutex_unlock(&f->lock);
            return NULL;
        }
        int req = f->req, action = f->req_action;
        char path[512];
        snprintf(path, sizeof(path), "%s", f->req_path);
        unsigned char *data = f->req_data;
        size_t data_len = f->req_len;
        f->req_data = NULL;
        f->req = REQ_NONE;
        f->busy = 1;
        pthread_mutex_unlock(&f->lock);

        files_result res;
        memset(&res, 0, sizeof(res));
        snprintf(res.path, sizeof(res.path), "%s", path);
        if (req == REQ_LIST) {
            res.entries = malloc(sizeof(ftp_entry) * MAX_ENTRIES);
            int n = res.entries ? ftp_list(f->host, f->password[0] ? f->password : NULL, path,
                                           res.entries, MAX_ENTRIES, res.message,
                                           sizeof(res.message)) : -1;
            if (n < 0) {
                free(res.entries);
                res.entries = NULL;
                res.kind = FR_ERROR;
            } else {
                res.kind = FR_LISTING;
                res.count = n;
            }
        } else if (req == REQ_DATA) {
            res.kind = do_data_action(f, path, data, data_len, action, res.message,
                                      sizeof(res.message)) == 0 ? FR_DONE : FR_ERROR;
            free(data);
        } else {
            res.kind = do_action(f, path, action, res.message, sizeof(res.message)) == 0
                       ? FR_DONE : FR_ERROR;
        }

        pthread_mutex_lock(&f->lock);
        if (f->have_result)
            free(f->result.entries);    /* superseded, never collected */
        f->result = res;
        f->have_result = 1;
        f->busy = 0;
        pthread_mutex_unlock(&f->lock);
    }
}

c64files *files_start(const char *host, const char *password, remote *rem)
{
    c64files *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    snprintf(f->host, sizeof(f->host), "%s", host);
    if (password)
        snprintf(f->password, sizeof(f->password), "%s", password);
    f->api.host = f->host;
    f->api.password = f->password[0] ? f->password : NULL;
    f->rem = rem;
    pthread_mutex_init(&f->lock, NULL);
    pthread_cond_init(&f->wake, NULL);
    if (pthread_create(&f->thread, NULL, worker, f) != 0) {
        free(f);
        return NULL;
    }
    return f;
}

void files_stop(c64files *f)
{
    if (!f)
        return;
    pthread_mutex_lock(&f->lock);
    f->quit = 1;
    pthread_cond_signal(&f->wake);
    pthread_mutex_unlock(&f->lock);
    pthread_join(f->thread, NULL);
    free(f->req_data);
    if (f->have_result)
        free(f->result.entries);
    pthread_mutex_destroy(&f->lock);
    pthread_cond_destroy(&f->wake);
    free(f);
}

static void request(c64files *f, int req, const char *path, int action)
{
    pthread_mutex_lock(&f->lock);
    free(f->req_data);                  /* a pending upload superseded by this request */
    f->req_data = NULL;
    f->req = req;
    f->req_action = action;
    snprintf(f->req_path, sizeof(f->req_path), "%s", path);
    pthread_cond_signal(&f->wake);
    pthread_mutex_unlock(&f->lock);
}

void files_list(c64files *f, const char *path)
{
    request(f, REQ_LIST, path, 0);
}

void files_action(c64files *f, const char *path, int action)
{
    request(f, REQ_ACTION, path, action);
}

void files_action_data(c64files *f, const char *name, unsigned char *data, size_t len, int action)
{
    pthread_mutex_lock(&f->lock);
    free(f->req_data);                  /* a pending upload superseded by this one */
    f->req = REQ_DATA;
    f->req_action = action;
    snprintf(f->req_path, sizeof(f->req_path), "%s", name);
    f->req_data = data;
    f->req_len = len;
    pthread_cond_signal(&f->wake);
    pthread_mutex_unlock(&f->lock);
}

int files_busy(c64files *f)
{
    pthread_mutex_lock(&f->lock);
    int b = f->busy || f->req != REQ_NONE;
    pthread_mutex_unlock(&f->lock);
    return b;
}

int files_poll(c64files *f, files_result *res)
{
    pthread_mutex_lock(&f->lock);
    int have = f->have_result;
    if (have) {
        *res = f->result;
        f->have_result = 0;
    }
    pthread_mutex_unlock(&f->lock);
    return have;
}
