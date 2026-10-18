#include "c64remote.h"

#include "c64api.h"
#include "c64keys.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define KEYQ_CAP   256
#define MSGQ_CAP   4
#define KBUF_ADDR  0x0277
#define KBUF_COUNT 0x00C6
#define KBUF_MAX   10

typedef struct { int key, mods; } keytap;

struct remote {
    c64api          api;
    char            host[64];
    char            password[64];
    pthread_t       thread;
    pthread_mutex_t lock;
    pthread_cond_t  wake;

    /* Shared with the UI thread, guarded by lock. */
    int      mode;
    int      quit;
    keytap   keys[KEYQ_CAP];
    int      key_head, key_len;
    int      joy_port;
    unsigned joy_wanted;
    int      menu_requests;
    int      release_all;
    char     msgs[MSGQ_CAP][96];
    int      msg_len;
    int      warned_restore;

    /* Local machine (the built-in emulator): input goes to these, no thread. */
    remote_sink sink;
    int         local;
};

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000);
}

/* ---- pure helpers ------------------------------------------------------- */

static const char *const joy_names[5] = { "up", "down", "left", "right", "fire" };
static const char *const tr_names[3] = { "press", "release", "tap" };

size_t remote_events_json(char *out, size_t cap, const remote_event *ev, int n)
{
    size_t len = 0;
#define PUT(...) do { int w = snprintf(out + len, cap > len ? cap - len : 0, __VA_ARGS__); \
                      if (w < 0 || len + (size_t)w >= cap) return 0; len += (size_t)w; } while (0)
    PUT("{\"events\":[");
    for (int i = 0; i < n; i++) {
        const remote_event *e = &ev[i];
        PUT("%s{", i ? "," : "");
        if (e->kind == EV_RELEASE_ALL) {
            PUT("\"kind\":\"release_all\"}");
            continue;
        }
        if (e->kind == EV_JOYSTICK)
            PUT("\"kind\":\"joystick\",\"port\":%d,", e->port);
        else
            PUT("\"kind\":\"keyboard\",");
        PUT("\"inputs\":[");
        for (int j = 0; j < e->count; j++)
            PUT("%s\"%s\"", j ? "," : "", e->names[j]);
        PUT("],\"transition\":\"%s\"}", tr_names[e->transition]);
    }
    PUT("]}");
#undef PUT
    return len;
}

static int joy_event(remote_event *e, int port, int transition, unsigned bits)
{
    memset(e, 0, sizeof(*e));
    e->kind = EV_JOYSTICK;
    e->transition = transition;
    e->port = port;
    for (int b = 0; b < 5; b++)
        if (bits & (1u << b))
            e->names[e->count++] = joy_names[b];
    return e->count > 0;
}

int joy_plan(joy_track *t, int port, unsigned wanted, uint32_t now, remote_event *out)
{
    int n = 0;
    if (t->port != port) {
        if (t->sent && joy_event(&out[n], t->port, TR_RELEASE, t->sent))
            n++;
        t->sent = 0;
        t->port = port;
        return n;   /* presses on the new port go in the next request */
    }

    unsigned press = wanted & ~t->sent, release = 0;
    for (int b = 0; b < 5; b++) {
        unsigned bit = 1u << b;
        if ((t->sent & bit) && !(wanted & bit) &&
            (uint32_t)(now - t->pressed_at[b]) >= JOY_MIN_HOLD_MS)
            release |= bit;
    }
    if (release && joy_event(&out[n], port, TR_RELEASE, release))
        n++;
    if (press && joy_event(&out[n], port, TR_PRESS, press)) {
        n++;
        for (int b = 0; b < 5; b++)
            if (press & (1u << b))
                t->pressed_at[b] = now;
    }
    t->sent = (t->sent & ~release) | press;
    return n;
}

/* ---- worker ------------------------------------------------------------- */

static void post_message(remote *r, const char *msg)
{
    pthread_mutex_lock(&r->lock);
    if (r->msg_len < MSGQ_CAP)
        snprintf(r->msgs[r->msg_len++], sizeof(r->msgs[0]), "%s", msg);
    pthread_mutex_unlock(&r->lock);
}

static int menu_button(remote *r)
{
    return c64api_request(&r->api, "PUT", "/v1/machine:menu_button", NULL, NULL, 0, NULL, 0, NULL);
}

static int post_events(remote *r, const remote_event *ev, int n)
{
    char body[4096];
    size_t len = remote_events_json(body, sizeof(body), ev, n);
    if (!len)
        return -1;
    return c64api_request(&r->api, "POST", "/v1/machine:input", "application/json",
                          body, len, NULL, 0, NULL);
}

static int write_mem(remote *r, int addr, const unsigned char *data, int n)
{
    char path[96];
    int len = snprintf(path, sizeof(path), "/v1/machine:writemem?address=%04X&data=", addr);
    for (int i = 0; i < n; i++)
        len += snprintf(path + len, sizeof(path) - (size_t)len, "%02X", data[i]);
    int status = c64api_request(&r->api, "PUT", path, NULL, NULL, 0, NULL, 0, NULL);
    return status >= 200 && status < 300 ? 0 : -1;
}

/* Typing-buffer mode: inject queued keys once the KERNAL buffer is empty. */
static void buffer_step(remote *r, int *retry_ms)
{
    unsigned char chars[KBUF_MAX];
    int n = 0;

    pthread_mutex_lock(&r->lock);
    int peek = 0;
    while (peek < r->key_len && n < KBUF_MAX) {
        keytap k = r->keys[(r->key_head + peek) % KEYQ_CAP];
        int c = c64key_petscii(k.key, k.mods);
        if (c >= 0)
            chars[n++] = (unsigned char)c;
        else if (k.key == C64K_RESTORE && !r->warned_restore) {
            r->warned_restore = 1;
            pthread_mutex_unlock(&r->lock);
            post_message(r, "RESTORE needs newer C64 firmware");
            pthread_mutex_lock(&r->lock);
        }
        peek++;
    }
    pthread_mutex_unlock(&r->lock);
    if (peek == 0)
        return;

    if (n > 0) {
        unsigned char count = 0xff;
        size_t got = 0;
        int status = c64api_request(&r->api, "GET", "/v1/machine:readmem?address=00C6&length=1",
                                    NULL, NULL, 0, &count, 1, &got);
        if (status != 200 || got != 1) {
            *retry_ms = 500;
            return;
        }
        /* Only write into an empty buffer: the KERNAL has nothing to consume,
         * so it can't shift the buffer under us between our two writes. */
        if (count != 0) {
            *retry_ms = 30;
            return;
        }
        unsigned char cnt = (unsigned char)n;
        if (write_mem(r, KBUF_ADDR, chars, n) != 0 || write_mem(r, KBUF_COUNT, &cnt, 1) != 0) {
            *retry_ms = 500;
            return;
        }
    }

    pthread_mutex_lock(&r->lock);
    r->key_head = (r->key_head + peek) % KEYQ_CAP;
    r->key_len -= peek;
    if (r->key_len)
        *retry_ms = 30;
    pthread_mutex_unlock(&r->lock);
}

/* Input-command mode: one batch per pass, one change per input per batch. */
static void input_step(remote *r, joy_track *joy, int *need_release_all, int *retry_ms)
{
    remote_event ev[64];
    int n = 0, taken = 0;

    pthread_mutex_lock(&r->lock);
    if (r->release_all) {
        r->release_all = 0;
        *need_release_all = 1;
    }
    if (*need_release_all) {
        ev[n++].kind = EV_RELEASE_ALL;
        memset(joy, 0, sizeof(*joy));
        joy->port = r->joy_port;
    }
    const char *used[64];
    int nused = 0;
    while (taken < r->key_len && n < REMOTE_MAX_KEY_EVENTS) {
        keytap k = r->keys[(r->key_head + taken) % KEYQ_CAP];
        remote_event *e = &ev[n];
        memset(e, 0, sizeof(*e));
        e->kind = EV_KEYBOARD;
        e->transition = TR_TAP;
        e->count = c64key_api_inputs(k.key, k.mods, e->names);
        /* A second tap of a key already in this batch waits for the next one. */
        int clash = 0;
        for (int i = 0; i < e->count && !clash; i++)
            for (int j = 0; j < nused; j++)
                if (!strcmp(e->names[i], used[j])) { clash = 1; break; }
        if (clash || nused + e->count > 64 || (k.key == C64K_RESTORE && n > 0))
            break;
        for (int i = 0; i < e->count; i++)
            used[nused++] = e->names[i];
        if (e->count)
            n++;
        taken++;
        if (k.key == C64K_RESTORE)
            break;                  /* restore goes alone */
    }
    int port = r->joy_port;
    unsigned wanted = r->joy_wanted;
    pthread_mutex_unlock(&r->lock);

    n += joy_plan(joy, port, wanted, now_ms(), &ev[n]);
    if (n == 0 && taken == 0) {
        if (joy->sent & ~wanted)
            *retry_ms = 10;         /* a release waiting out its minimum hold */
        return;
    }

    int status = n ? post_events(r, ev, n) : 200;
    if (status != 200) {
        /* Start over from a clean slate; held inputs get pressed again. */
        *need_release_all = 1;
        *retry_ms = 200;
        return;
    }
    *need_release_all = 0;

    pthread_mutex_lock(&r->lock);
    r->key_head = (r->key_head + taken) % KEYQ_CAP;
    r->key_len -= taken;
    if (r->key_len || r->joy_wanted != joy->sent || r->joy_port != joy->port)
        *retry_ms = r->key_len ? 0 : 10;
    pthread_mutex_unlock(&r->lock);
}

static int detect(remote *r)
{
    char body[512];
    size_t got;
    int status = c64api_request(&r->api, "GET", "/v1/machine:input", NULL, NULL, 0,
                                body, sizeof(body), &got);
    if (status == 200)
        return REMOTE_INPUT;
    if (status < 0)
        return REMOTE_UNREACHABLE;
    return REMOTE_BUFFER;           /* 404 (no such route) or 501 (no input hardware) */
}

static void *worker(void *arg)
{
    remote *r = arg;
    int mode = REMOTE_DETECTING;
    while (mode == REMOTE_DETECTING || mode == REMOTE_UNREACHABLE) {
        int m = detect(r);
        pthread_mutex_lock(&r->lock);
        r->mode = m;
        int quit = r->quit;
        pthread_mutex_unlock(&r->lock);
        if (m != mode)
            post_message(r, m == REMOTE_INPUT  ? "Controller: joystick and keys enabled" :
                            m == REMOTE_BUFFER ? "Controller: keys only (joystick needs newer C64 firmware)" :
                                                 "Controller: can't reach the C64");
        mode = m;
        if (quit)
            return NULL;
        if (mode == REMOTE_UNREACHABLE) {
            struct timespec ts = { 3, 0 };
            nanosleep(&ts, NULL);
        }
    }

    joy_track joy;
    memset(&joy, 0, sizeof(joy));
    joy.port = 2;
    int need_release_all = mode == REMOTE_INPUT;

    for (;;) {
        pthread_mutex_lock(&r->lock);
        int quit = r->quit;
        int menus = r->menu_requests;
        r->menu_requests = 0;
        pthread_mutex_unlock(&r->lock);

        for (int i = 0; i < menus; i++)
            menu_button(r);

        int retry_ms = -1;
        if (mode == REMOTE_INPUT) {
            input_step(r, &joy, &need_release_all, &retry_ms);
            if (quit) {
                remote_event e = { .kind = EV_RELEASE_ALL };
                post_events(r, &e, 1);
                return NULL;
            }
        } else {
            buffer_step(r, &retry_ms);
            if (quit)
                return NULL;
        }

        /* Sleep until new input arrives or a retry is due. */
        pthread_mutex_lock(&r->lock);
        int idle = !r->quit && !r->menu_requests && !r->release_all &&
                   (mode == REMOTE_INPUT ? r->key_len == 0 && r->joy_wanted == joy.sent &&
                                           r->joy_port == joy.port
                                         : r->key_len == 0);
        if (retry_ms > 0 || (retry_ms < 0 && idle)) {
            if (retry_ms > 0) {
                struct timeval tv;
                gettimeofday(&tv, NULL);
                struct timespec until;
                long ns = tv.tv_usec * 1000L + (long)retry_ms * 1000000L;
                until.tv_sec = tv.tv_sec + ns / 1000000000L;
                until.tv_nsec = ns % 1000000000L;
                pthread_cond_timedwait(&r->wake, &r->lock, &until);
            } else {
                pthread_cond_wait(&r->wake, &r->lock);
            }
        }
        pthread_mutex_unlock(&r->lock);
    }
}

/* ---- UI-thread API -------------------------------------------------------- */

remote *remote_start(const char *host, const char *password)
{
    remote *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    snprintf(r->host, sizeof(r->host), "%s", host);
    r->api.host = r->host;
    if (password) {
        snprintf(r->password, sizeof(r->password), "%s", password);
        r->api.password = r->password;
    }
    r->mode = REMOTE_DETECTING;
    r->joy_port = 2;
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->wake, NULL);
    if (pthread_create(&r->thread, NULL, worker, r) != 0) {
        free(r);
        return NULL;
    }
    return r;
}

remote *remote_start_local(const remote_sink *sink)
{
    remote *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    r->sink = *sink;
    r->local = 1;
    r->mode = REMOTE_INPUT;               /* real keys and joystick */
    r->joy_port = 2;
    pthread_mutex_init(&r->lock, NULL);
    pthread_cond_init(&r->wake, NULL);
    return r;
}

void remote_stop(remote *r)
{
    if (!r)
        return;
    if (r->local) {
        if (r->sink.release_all)
            r->sink.release_all();
        pthread_mutex_destroy(&r->lock);
        pthread_cond_destroy(&r->wake);
        free(r);
        return;
    }
    pthread_mutex_lock(&r->lock);
    r->quit = 1;
    r->joy_wanted = 0;
    pthread_cond_signal(&r->wake);
    pthread_mutex_unlock(&r->lock);
    pthread_join(r->thread, NULL);
    pthread_mutex_destroy(&r->lock);
    pthread_cond_destroy(&r->wake);
    free(r);
}

int remote_mode(remote *r)
{
    pthread_mutex_lock(&r->lock);
    int m = r->mode;
    pthread_mutex_unlock(&r->lock);
    return m;
}

void remote_key_tap(remote *r, int key, int mods)
{
    if (r->local) {
        if (r->sink.key_tap)
            r->sink.key_tap(key, mods);
        return;
    }
    pthread_mutex_lock(&r->lock);
    if (r->key_len < KEYQ_CAP) {
        r->keys[(r->key_head + r->key_len) % KEYQ_CAP] = (keytap){ key, mods };
        r->key_len++;
        pthread_cond_signal(&r->wake);
    }
    pthread_mutex_unlock(&r->lock);
}

void remote_joystick(remote *r, int port, unsigned bits)
{
    if (r->local) {
        if (r->sink.joystick)
            r->sink.joystick(port, bits);
        return;
    }
    pthread_mutex_lock(&r->lock);
    if (r->joy_port != port || r->joy_wanted != bits) {
        r->joy_port = port;
        r->joy_wanted = bits;
        pthread_cond_signal(&r->wake);
    }
    pthread_mutex_unlock(&r->lock);
}

void remote_menu_button(remote *r)
{
    if (r->local) {
        post_message(r, "The built-in emulator has no Ultimate menu");
        return;
    }
    pthread_mutex_lock(&r->lock);
    r->menu_requests++;
    pthread_cond_signal(&r->wake);
    pthread_mutex_unlock(&r->lock);
}

void remote_release_all(remote *r)
{
    if (r->local) {
        if (r->sink.release_all)
            r->sink.release_all();
        return;
    }
    pthread_mutex_lock(&r->lock);
    r->release_all = 1;
    r->joy_wanted = 0;
    pthread_cond_signal(&r->wake);
    pthread_mutex_unlock(&r->lock);
}

int remote_take_message(remote *r, char *out, size_t cap)
{
    pthread_mutex_lock(&r->lock);
    int have = r->msg_len > 0;
    if (have) {
        snprintf(out, cap, "%s", r->msgs[0]);
        memmove(r->msgs[0], r->msgs[1], sizeof(r->msgs[0]) * (size_t)(MSGQ_CAP - 1));
        r->msg_len--;
    }
    pthread_mutex_unlock(&r->lock);
    return have;
}
