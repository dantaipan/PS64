#include "c64remote.h"

#include "c64api.h"
#include "c64keys.h"

#include <ctype.h>
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

#define MOUSE_MAX_MOTION   127
#define MOUSE_MAX_WHEEL    64
#define MOUSE_REPORT_MS    20      /* the firmware sends one queued mouse report per 20 ms */
#define MOUSE_MAX_PENDING  2       /* reports queued at the C64 before motion waits */
#define MENU_POLL_MS       1000
#define MENU_FAST_POLL_MS  250     /* for a while after the menu button */
#define MENU_FAST_FOR_MS   3000

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
    remote_keytap keys[KEYQ_CAP];
    int      key_head, key_len;
    int      joy_port;
    unsigned joy_wanted;
    int      menu_requests;
    int      release_all;
    int      resync;
    char     msgs[MSGQ_CAP][96];
    int      msg_len;
    int      warned_restore;
    int      menu_open;
    int      has_mouse;
    int      mouse_on;
    unsigned mouse_wanted;
    int      mouse_dx, mouse_dy, mouse_wheel;

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
static const char *const mouse_names[3] = { "left", "right", "middle" };
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
        switch (e->kind) {
        case EV_RELEASE_ALL:
            PUT("\"kind\":\"release_all\"}");
            continue;
        case EV_MOUSE_MOVE:
            PUT("\"kind\":\"mouse\",\"move\":{\"x\":%d,\"y\":%d}}", e->x, e->y);
            continue;
        case EV_MOUSE_WHEEL:
            PUT("\"kind\":\"mouse\",\"wheel\":{\"vertical\":%d}}", e->y);
            continue;
        case EV_JOYSTICK:
            PUT("\"kind\":\"joystick\",\"port\":%d,", e->port);
            break;
        case EV_MOUSE:
            PUT("\"kind\":\"mouse\",");
            break;
        default:
            PUT("\"kind\":\"keyboard\",");
            break;
        }
        PUT("\"inputs\":[");
        for (int j = 0; j < e->count; j++)
            PUT("%s\"%s\"", j ? "," : "", e->names[j]);
        PUT("],\"transition\":\"%s\"}", tr_names[e->transition]);
    }
    PUT("]}");
#undef PUT
    return len;
}

static int bits_event(remote_event *e, int kind, int port, int transition, unsigned bits,
                      const char *const *names, int nbits)
{
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->transition = transition;
    e->port = port;
    for (int b = 0; b < nbits; b++)
        if (bits & (1u << b))
            e->names[e->count++] = names[b];
    return e->count > 0;
}

/* Presses at once, releases after the minimum hold. */
static int bits_plan(joy_track *t, unsigned wanted, uint32_t now, int kind, int port,
                     const char *const *names, int nbits, remote_event *out)
{
    int n = 0;
    unsigned mask = (1u << nbits) - 1;
    wanted &= mask;
    unsigned press = wanted & ~t->sent, release = 0;
    for (int b = 0; b < nbits; b++) {
        unsigned bit = 1u << b;
        if ((t->sent & bit) && !(wanted & bit) &&
            (uint32_t)(now - t->pressed_at[b]) >= JOY_MIN_HOLD_MS)
            release |= bit;
    }
    if (release && bits_event(&out[n], kind, port, TR_RELEASE, release, names, nbits))
        n++;
    if (press && bits_event(&out[n], kind, port, TR_PRESS, press, names, nbits)) {
        n++;
        for (int b = 0; b < nbits; b++)
            if (press & (1u << b))
                t->pressed_at[b] = now;
    }
    t->sent = (t->sent & ~release) | press;
    return n;
}

int joy_plan(joy_track *t, int port, unsigned wanted, uint32_t now, remote_event *out)
{
    if (t->port != port) {
        int n = 0;
        if (t->sent && bits_event(&out[n], EV_JOYSTICK, t->port, TR_RELEASE, t->sent, joy_names, 5))
            n++;
        t->sent = 0;
        t->port = port;
        return n;   /* presses on the new port go in the next request */
    }
    return bits_plan(t, wanted, now, EV_JOYSTICK, port, joy_names, 5, out);
}

int mouse_plan(joy_track *t, unsigned wanted, uint32_t now, remote_event *out)
{
    return bits_plan(t, wanted, now, EV_MOUSE, 0, mouse_names, 3, out);
}

static int clamp(int v, int lim) { return v < -lim ? -lim : v > lim ? lim : v; }

int mouse_motion(int *dx, int *dy, remote_event *out)
{
    if (!*dx && !*dy)
        return 0;
    memset(out, 0, sizeof(*out));
    out->kind = EV_MOUSE_MOVE;
    out->x = clamp(*dx, MOUSE_MAX_MOTION);
    out->y = clamp(*dy, MOUSE_MAX_MOTION);
    *dx -= out->x;
    *dy -= out->y;
    return 1;
}

/* The value after "key": in a JSON body, or NULL. */
static const char *json_value(const char *body, const char *key)
{
    size_t klen = strlen(key);
    for (const char *p = body ? strchr(body, '"') : NULL; p; p = strchr(p + 1, '"')) {
        if (strncmp(p + 1, key, klen) || p[1 + klen] != '"')
            continue;
        const char *v = p + 2 + klen;
        while (isspace((unsigned char)*v))
            v++;
        if (*v != ':')
            continue;
        v++;
        while (isspace((unsigned char)*v))
            v++;
        return v;
    }
    return NULL;
}

int remote_parse_pending(const char *body)
{
    const char *v = json_value(body, "pending");
    if (!v || !isdigit((unsigned char)*v))
        return -1;
    return atoi(v);
}

int remote_reply_has_mouse(const char *body)
{
    const char *v = json_value(body, "mouse");
    return v && *v == '{';
}

/* Names in this request already: a second change of one waits for the next. */
typedef struct { const char *names[64]; int n; } name_set;

static int names_clash(const name_set *s, const remote_event *e)
{
    for (int i = 0; i < e->count; i++)
        for (int j = 0; j < s->n; j++)
            if (!strcmp(e->names[i], s->names[j]))
                return 1;
    return s->n + e->count > 64;
}

static void names_add(name_set *s, const remote_event *e)
{
    for (int i = 0; i < e->count; i++)
        s->names[s->n++] = e->names[i];
}

static int key_held(const key_track *t, int key)
{
    return key >= 0 && key < 64 && (t->held >> key) & 1;
}

int keys_plan(key_track *t, const remote_keytap *q, int qlen, uint32_t now,
              remote_event *out, int max, int *taken, int *wait_ms)
{
    name_set used = { .n = 0 };
    int n = 0;
    *taken = 0;
    *wait_ms = 0;
    while (*taken < qlen && n < max) {
        remote_keytap k = q[*taken];
        remote_event *e = &out[n];
        memset(e, 0, sizeof(*e));
        e->kind = EV_KEYBOARD;
        int restore = k.key == C64K_RESTORE;

        if (restore) {
            if (k.transition == TR_RELEASE) {       /* (only a tap does anything) */
                (*taken)++;
                continue;
            }
            if (n > 0)
                break;                              /* restore goes alone */
            e->transition = TR_TAP;
            e->names[e->count++] = c64key_api_name(C64K_RESTORE);
            n++;
            (*taken)++;
            break;
        }

        if (k.transition == TR_TAP) {
            const char *names[4];
            int count = key_held(t, k.key) ? 0 : c64key_api_inputs(k.key, k.mods, names);
            static const int mods[3] = { C64K_LSHIFT, C64K_CBM, C64K_CTRL };
            for (int i = 0; i < count; i++) {
                int held_mod = 0;
                for (int m = 0; m < 3; m++)
                    if (key_held(t, mods[m]) && !strcmp(names[i], c64key_api_name(mods[m])))
                        held_mod = 1;
                if (!held_mod)
                    e->names[e->count++] = names[i];
            }
            e->transition = TR_TAP;
            if (!e->count) {
                (*taken)++;
                continue;
            }
            if (names_clash(&used, e))
                break;
            names_add(&used, e);
            n++;
            (*taken)++;
            continue;
        }

        if (k.key < 0 || k.key >= 64) {
            (*taken)++;
            continue;
        }
        int held = key_held(t, k.key);
        if ((k.transition == TR_PRESS) == held) {   /* (no change) */
            (*taken)++;
            continue;
        }
        if (k.transition == TR_RELEASE) {
            uint32_t since = now - t->pressed_at[k.key];
            if (since < JOY_MIN_HOLD_MS) {
                *wait_ms = (int)(JOY_MIN_HOLD_MS - since);
                break;
            }
        }
        e->transition = k.transition;
        e->names[e->count++] = c64key_api_name(k.key);
        if (names_clash(&used, e))
            break;
        names_add(&used, e);
        if (k.transition == TR_PRESS) {
            t->held |= 1ull << k.key;
            t->pressed_at[k.key] = now;
        } else {
            t->held &= ~(1ull << k.key);
        }
        n++;
        (*taken)++;
    }
    return n;
}

int keys_repress(const key_track *t, remote_event *out, int max)
{
    int n = 0;
    for (int k = 0; k < 64 && n <= max; k++) {
        if (!key_held(t, k))
            continue;
        if (n == 0 || out[n - 1].count == 8) {
            if (n == max)
                break;
            memset(&out[n], 0, sizeof(out[n]));
            out[n].kind = EV_KEYBOARD;
            out[n].transition = TR_PRESS;
            n++;
        }
        out[n - 1].names[out[n - 1].count++] = c64key_api_name(k);
    }
    return n;
}

/* ---- worker ------------------------------------------------------------- */

typedef struct {
    joy_track joy, mouse;
    key_track keys;
    int       need_resync;         /* send release_all, then press what's held again */
    int       pending;             /* mouse reports the C64 had queued (-1 unknown) */
    uint32_t  pending_at;
} wstate;

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

/* POST the events; *pending gets mouse.pending from the reply (-1 if none). */
static int post_events(remote *r, const remote_event *ev, int n, int *pending)
{
    char body[4096], reply[2048];
    size_t got = 0;
    size_t len = remote_events_json(body, sizeof(body), ev, n);
    if (pending)
        *pending = -1;
    if (!len)
        return -1;
    int status = c64api_request(&r->api, "POST", "/v1/machine:input", "application/json",
                                body, len, reply, sizeof(reply) - 1, &got);
    reply[got] = 0;
    if (pending && status == 200)
        *pending = remote_parse_pending(reply);
    return status;
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
        remote_keytap k = r->keys[(r->key_head + peek) % KEYQ_CAP];
        int c = k.transition == TR_TAP ? c64key_petscii(k.key, k.mods) : -1;
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

static int min_retry(int a, int b) { return a < 0 ? b : b < 0 ? a : a < b ? a : b; }

/* Mouse reports still queued at the C64, as estimated from the last reply. */
static int pending_now(const wstate *w, uint32_t now)
{
    if (w->pending < 0)
        return -1;
    int left = w->pending - (int)((now - w->pending_at) / MOUSE_REPORT_MS);
    return left > 0 ? left : 0;
}

static void set_pending(wstate *w, int pending)
{
    w->pending = pending;
    w->pending_at = now_ms();
}

/* Input-command mode: one batch per pass, one change per input per batch. */
static void input_step(remote *r, wstate *w, int *retry_ms)
{
    remote_event ev[64];
    remote_keytap q[REMOTE_MAX_KEY_EVENTS * 2];
    int n = 0, qn = 0;
    uint32_t now = now_ms();

    pthread_mutex_lock(&r->lock);
    if (r->release_all) {
        r->release_all = 0;
        memset(&w->keys, 0, sizeof(w->keys));
        w->need_resync = 1;
    }
    if (r->resync) {
        r->resync = 0;
        w->need_resync = 1;
    }
    while (qn < r->key_len && qn < (int)(sizeof(q) / sizeof(q[0]))) {
        q[qn] = r->keys[(r->key_head + qn) % KEYQ_CAP];
        qn++;
    }
    int port = r->joy_port;
    unsigned wanted = r->joy_wanted;
    int mouse_on = r->mouse_on;
    unsigned mouse_wanted = mouse_on ? r->mouse_wanted : 0;
    int dx = mouse_on ? r->mouse_dx : 0, dy = mouse_on ? r->mouse_dy : 0;
    int wheel = mouse_on ? r->mouse_wheel : 0;
    pthread_mutex_unlock(&r->lock);

    wstate before = *w;
    int resync = w->need_resync;
    if (resync) {
        memset(&ev[n++], 0, sizeof(ev[0]));
        ev[0].kind = EV_RELEASE_ALL;
        memset(&w->joy, 0, sizeof(w->joy));
        w->joy.port = port;
        memset(&w->mouse, 0, sizeof(w->mouse));
        set_pending(w, 0);
        n += keys_repress(&w->keys, &ev[n], 2);
    }

    int taken = 0, key_wait = 0;
    n += keys_plan(&w->keys, q, qn, now, &ev[n], REMOTE_MAX_KEY_EVENTS, &taken, &key_wait);
    n += joy_plan(&w->joy, port, wanted, now, &ev[n]);
    n += mouse_plan(&w->mouse, mouse_wanted, now, &ev[n]);

    int sent_x = 0, sent_y = 0, sent_wheel = 0, motion_wait = 0;
    if (dx || dy) {
        int pending = pending_now(w, now);
        if (pending >= 0 && pending <= MOUSE_MAX_PENDING) {
            int lx = dx, ly = dy;
            if (mouse_motion(&lx, &ly, &ev[n])) {
                sent_x = dx - lx;
                sent_y = dy - ly;
                n++;
            }
        } else {
            motion_wait = 1;
        }
    }
    if (wheel) {
        memset(&ev[n], 0, sizeof(ev[0]));
        ev[n].kind = EV_MOUSE_WHEEL;
        ev[n].y = sent_wheel = clamp(wheel, MOUSE_MAX_WHEEL);
        n++;
    }

    int held_back = ((w->joy.sent & ~wanted) || (w->mouse.sent & ~mouse_wanted)) ? 10 : -1;
    if (n == 0 && taken == 0) {
        if (motion_wait) {
            /* The C64's queue was full, or its length unknown: ask it how long it is now. */
            char reply[2048];
            size_t got = 0;
            if (pending_now(w, now) < 0 &&
                c64api_request(&r->api, "GET", "/v1/machine:input", NULL, NULL, 0,
                               reply, sizeof(reply) - 1, &got) == 200) {
                reply[got] = 0;
                set_pending(w, remote_parse_pending(reply));
            }
            *retry_ms = MOUSE_REPORT_MS;
        }
        *retry_ms = min_retry(*retry_ms, held_back);
        if (key_wait)
            *retry_ms = min_retry(*retry_ms, key_wait);
        return;
    }

    int pending = -1;
    int status = n ? post_events(r, ev, n, &pending) : 200;
    if (status == 429) {
        /* The mouse queue is full; nothing in the batch was applied. */
        *w = before;
        set_pending(w, -1);
        *retry_ms = 2 * MOUSE_REPORT_MS;
        return;
    }
    if (status != 200) {
        /* Start over from a clean slate; held inputs get pressed again. */
        w->need_resync = 1;
        *retry_ms = 200;
        return;
    }
    w->need_resync = 0;
    if (pending >= 0 || n)
        set_pending(w, pending);

    pthread_mutex_lock(&r->lock);
    r->key_head = (r->key_head + taken) % KEYQ_CAP;
    r->key_len -= taken;
    if (r->mouse_on) {
        r->mouse_dx -= sent_x;
        r->mouse_dy -= sent_y;
        r->mouse_wheel -= sent_wheel;
    }
    if (r->key_len)
        *retry_ms = key_wait;       /* (0: at once; else a release waiting out its hold) */
    if (r->joy_wanted != w->joy.sent || r->joy_port != w->joy.port ||
        (r->mouse_on ? r->mouse_wanted : 0) != w->mouse.sent)
        *retry_ms = min_retry(*retry_ms, 10);
    if (r->mouse_on && (r->mouse_dx || r->mouse_dy || r->mouse_wheel))
        *retry_ms = min_retry(*retry_ms, pending_now(w, now_ms()) > MOUSE_MAX_PENDING ? MOUSE_REPORT_MS : 0);
    pthread_mutex_unlock(&r->lock);
}

static int detect(remote *r)
{
    char body[2048];
    size_t got = 0;
    int status = c64api_request(&r->api, "GET", "/v1/machine:input", NULL, NULL, 0,
                                body, sizeof(body) - 1, &got);
    body[got] = 0;
    if (status == 200) {
        pthread_mutex_lock(&r->lock);
        r->has_mouse = remote_reply_has_mouse(body);
        pthread_mutex_unlock(&r->lock);
        return REMOTE_INPUT;
    }
    if (status < 0)
        return REMOTE_UNREACHABLE;
    return REMOTE_BUFFER;           /* 404 (no such route) or 501 (no input hardware) */
}

/* Ask whether the Ultimate menu is open. Returns 0 if the firmware has no such route. */
static int poll_menu(remote *r)
{
    char b[16];
    size_t got = 0;
    int status = c64api_request(&r->api, "GET", "/v1/machine:menu_screen", NULL, NULL, 0,
                                b, sizeof(b), &got);
    if (status == 404 && got == 0)
        return 0;                   /* (the router's empty 404: firmware 1.1.0) */
    if (status == 200 || status == 404) {
        pthread_mutex_lock(&r->lock);
        r->menu_open = status == 200;
        pthread_mutex_unlock(&r->lock);
    }
    return 1;
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
        int mouse = r->has_mouse;
        pthread_mutex_unlock(&r->lock);
        if (m != mode)
            post_message(r, m == REMOTE_INPUT  ? (mouse ? "Controller: joystick, keys and mouse enabled"
                                                        : "Controller: joystick and keys enabled") :
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

    wstate w;
    memset(&w, 0, sizeof(w));
    w.joy.port = 2;
    w.need_resync = mode == REMOTE_INPUT;
    int menu_route = mode == REMOTE_INPUT;
    uint32_t next_menu_poll = now_ms(), menu_fast_until = 0;

    for (;;) {
        pthread_mutex_lock(&r->lock);
        int quit = r->quit;
        int menus = r->menu_requests;
        r->menu_requests = 0;
        pthread_mutex_unlock(&r->lock);

        for (int i = 0; i < menus; i++)
            menu_button(r);
        if (menus) {
            menu_fast_until = now_ms() + MENU_FAST_FOR_MS;
            next_menu_poll = now_ms() + MENU_FAST_POLL_MS;
        }

        int retry_ms = -1, menu_wait = -1;
        if (mode == REMOTE_INPUT) {
            input_step(r, &w, &retry_ms);
            if (quit) {
                remote_event e = { .kind = EV_RELEASE_ALL };
                post_events(r, &e, 1, NULL);
                return NULL;
            }
            /* Is the Ultimate menu open? Asked when there's no input waiting. */
            uint32_t now = now_ms();
            pthread_mutex_lock(&r->lock);
            int waiting = r->key_len || r->release_all || r->resync || r->menu_requests;
            pthread_mutex_unlock(&r->lock);
            if (menu_route && !waiting && retry_ms != 0 && (int32_t)(now - next_menu_poll) >= 0) {
                menu_route = poll_menu(r);
                now = now_ms();
                next_menu_poll = now + ((int32_t)(now - menu_fast_until) < 0 ? MENU_FAST_POLL_MS
                                                                              : MENU_POLL_MS);
            }
            if (menu_route) {
                int32_t until = (int32_t)(next_menu_poll - now_ms());
                menu_wait = until > 0 ? until : 1;
            }
        } else {
            buffer_step(r, &retry_ms);
            if (quit)
                return NULL;
        }

        /* Sleep until new input arrives or a retry is due. */
        pthread_mutex_lock(&r->lock);
        int idle = !r->quit && !r->menu_requests && !r->release_all && !r->resync &&
                   (mode == REMOTE_INPUT ? r->key_len == 0 && r->joy_wanted == w.joy.sent &&
                                           r->joy_port == w.joy.port &&
                                           (r->mouse_on ? r->mouse_wanted : 0) == w.mouse.sent &&
                                           !(r->mouse_on && (r->mouse_dx || r->mouse_dy || r->mouse_wheel))
                                         : r->key_len == 0);
        if (retry_ms < 0 && idle && menu_wait > 0)
            retry_ms = menu_wait;   /* (idle until the next menu check) */
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
    r->has_mouse = sink->mouse_move != NULL;
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
        if (r->mouse_on && r->sink.mouse_enable)
            r->sink.mouse_enable(0);
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

static void queue_key(remote *r, int key, int mods, int transition)
{
    pthread_mutex_lock(&r->lock);
    if (r->key_len < KEYQ_CAP) {
        r->keys[(r->key_head + r->key_len) % KEYQ_CAP] = (remote_keytap){ key, mods, transition };
        r->key_len++;
        pthread_cond_signal(&r->wake);
    }
    pthread_mutex_unlock(&r->lock);
}

void remote_key_tap(remote *r, int key, int mods)
{
    if (r->local) {
        if (r->sink.key_tap)
            r->sink.key_tap(key, mods);
        return;
    }
    queue_key(r, key, mods, TR_TAP);
}

int remote_holds_keys(remote *r)
{
    return r && (r->local || remote_mode(r) == REMOTE_INPUT);
}

void remote_key(remote *r, int key, int down)
{
    if (r->local) {
        if (r->sink.key)
            r->sink.key(key, down);
        else if (down && r->sink.key_tap)
            r->sink.key_tap(key, 0);
        return;
    }
    if (remote_mode(r) == REMOTE_INPUT)
        queue_key(r, key, 0, down ? TR_PRESS : TR_RELEASE);
    else if (down && !c64key_is_modifier(key))
        queue_key(r, key, 0, TR_TAP);
}

int remote_local(remote *r) { return r && r->local; }

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
        if (r->sink.mouse_buttons)
            r->sink.mouse_buttons(0);
        return;
    }
    pthread_mutex_lock(&r->lock);
    r->release_all = 1;
    r->joy_wanted = 0;
    r->mouse_wanted = 0;
    r->mouse_dx = r->mouse_dy = r->mouse_wheel = 0;
    pthread_cond_signal(&r->wake);
    pthread_mutex_unlock(&r->lock);
}

void remote_resync(remote *r)
{
    if (!r || r->local)
        return;                     /* (the emulator keeps its own state) */
    pthread_mutex_lock(&r->lock);
    if (r->mode == REMOTE_INPUT) {
        r->resync = 1;
        pthread_cond_signal(&r->wake);
    }
    pthread_mutex_unlock(&r->lock);
}

int remote_menu_open(remote *r)
{
    if (!r || r->local)
        return 0;
    pthread_mutex_lock(&r->lock);
    int open = r->menu_open;
    pthread_mutex_unlock(&r->lock);
    return open;
}

int remote_has_mouse(remote *r)
{
    if (!r)
        return 0;
    pthread_mutex_lock(&r->lock);
    int has = r->has_mouse && r->mode == REMOTE_INPUT;
    pthread_mutex_unlock(&r->lock);
    return has;
}

void remote_mouse_enable(remote *r, int on)
{
    on = !!on;
    pthread_mutex_lock(&r->lock);
    int was = r->mouse_on;
    r->mouse_on = on;
    if (!on) {
        r->mouse_wanted = 0;
        r->mouse_dx = r->mouse_dy = r->mouse_wheel = 0;
        if (was && !r->local)
            r->resync = 1;          /* release_all unplugs the mouse; the rest is pressed again */
    }
    pthread_cond_signal(&r->wake);
    pthread_mutex_unlock(&r->lock);
    if (r->local && was != on && r->sink.mouse_enable)
        r->sink.mouse_enable(on);
}

void remote_mouse_move(remote *r, int dx, int dy)
{
    if (!dx && !dy)
        return;
    if (r->local) {
        if (r->mouse_on && r->sink.mouse_move)
            r->sink.mouse_move(dx, dy);
        return;
    }
    pthread_mutex_lock(&r->lock);
    if (r->mouse_on) {
        /* (bounded: a stalled connection doesn't store up a jump) */
        r->mouse_dx = clamp(r->mouse_dx + dx, 4 * MOUSE_MAX_MOTION);
        r->mouse_dy = clamp(r->mouse_dy + dy, 4 * MOUSE_MAX_MOTION);
        pthread_cond_signal(&r->wake);
    }
    pthread_mutex_unlock(&r->lock);
}

void remote_mouse_buttons(remote *r, unsigned bits)
{
    if (r->local) {
        if (r->mouse_on && r->sink.mouse_buttons)
            r->sink.mouse_buttons(bits);
        return;
    }
    pthread_mutex_lock(&r->lock);
    if (r->mouse_on && r->mouse_wanted != bits) {
        r->mouse_wanted = bits;
        pthread_cond_signal(&r->wake);
    }
    pthread_mutex_unlock(&r->lock);
}

void remote_mouse_wheel(remote *r, int detents)
{
    if (!detents || r->local)
        return;                     /* (VICE's 1351 has no wheel) */
    pthread_mutex_lock(&r->lock);
    if (r->mouse_on) {
        r->mouse_wheel = clamp(r->mouse_wheel + detents, MOUSE_MAX_WHEEL);
        pthread_cond_signal(&r->wake);
    }
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
