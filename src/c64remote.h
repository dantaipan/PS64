/*
 * c64remote - forwards keys, joystick and mouse to a C64 Ultimate from a background
 * thread, so the video loop never waits on the network (or, for the built-in emulator,
 * straight to it).
 *
 * At start it probes GET /v1/machine:input:
 *   200      -> REMOTE_INPUT: real key presses (held as long as they're held), joystick
 *               and RESTORE via POST /v1/machine:input (C64 Ultimate firmware 1.2.0+);
 *               a "mouse" object in the reply means a 1351 mouse on port 1 too (1.2.1+).
 *               It also asks GET /v1/machine:menu_screen now and then whether the
 *               Ultimate menu is open (200 open, 404 closed).
 *   404/501  -> REMOTE_BUFFER: PETSCII typed into the KERNAL keyboard buffer
 *               ($0277, count $C6) with readmem/writemem; no joystick (firmware 1.1.0)
 */
#ifndef C64REMOTE_H
#define C64REMOTE_H

#include <stddef.h>
#include <stdint.h>

enum { REMOTE_DETECTING, REMOTE_BUFFER, REMOTE_INPUT, REMOTE_UNREACHABLE };

/* Joystick bits. */
#define JOY_UP    1u
#define JOY_DOWN  2u
#define JOY_LEFT  4u
#define JOY_RIGHT 8u
#define JOY_FIRE  16u

/* Mouse button bits. */
#define MOUSE_LEFT   1u
#define MOUSE_RIGHT  2u
#define MOUSE_MIDDLE 4u

typedef struct remote remote;

remote *remote_start(const char *host, const char *password);

/* A machine in this process (the built-in emulator): taps and joystick
 * state go straight to these callbacks; the mode is REMOTE_INPUT. */
typedef struct {
    void (*key_tap)(int key, int mods);
    void (*joystick)(int port, unsigned bits);
    void (*release_all)(void);
    void (*key)(int key, int down);          /* a key held down / let go (may be NULL) */
    /* A 1351 mouse on port 1 (all NULL: the machine has none). */
    void (*mouse_enable)(int on);
    void (*mouse_move)(int dx, int dy);
    void (*mouse_buttons)(unsigned bits);
} remote_sink;
remote *remote_start_local(const remote_sink *sink);

void remote_stop(remote *r);               /* releases everything, joins the thread */
int  remote_mode(remote *r);

void remote_key_tap(remote *r, int key, int mods);
/* A key pressed or let go, held in between, on a machine that holds keys (remote_holds_keys:
 * a machine in this process, or a C64 Ultimate with the input command); on one typing into
 * the keyboard buffer a tap when pressed, modifier keys on their own doing nothing. */
void remote_key(remote *r, int key, int down);
int  remote_local(remote *r);
/* Keys pressed with remote_key stay down until let go. */
int  remote_holds_keys(remote *r);
void remote_joystick(remote *r, int port, unsigned bits);
void remote_menu_button(remote *r);
void remote_release_all(remote *r);
/* The machine dropped what it held (it was reset): press what's still held again. */
void remote_resync(remote *r);

/* The Ultimate menu is on screen (and takes the keyboard). 0 on a machine in this process. */
int  remote_menu_open(remote *r);

/* The machine takes a 1351 mouse on port 1 (C64 Ultimate firmware 1.2.1+, the emulator's
 * C64); 0 while that isn't known yet. */
int  remote_has_mouse(remote *r);
/* The mouse plugged in (on) or out: out lets its buttons go, and port 1 is a joystick port
 * again. */
void remote_mouse_enable(remote *r, int on);
/* Motion in mouse counts (+x right, +y down), added up until it's sent. */
void remote_mouse_move(remote *r, int dx, int dy);
void remote_mouse_buttons(remote *r, unsigned bits);
/* Wheel detents (+ away from the user). */
void remote_mouse_wheel(remote *r, int detents);

/* Pops one status message for the UI (e.g. "RESTORE needs newer firmware"). */
int remote_take_message(remote *r, char *out, size_t cap);

/* ---- pure helpers, exposed for unit tests ------------------------------ */

enum { EV_KEYBOARD, EV_JOYSTICK, EV_RELEASE_ALL, EV_MOUSE, EV_MOUSE_MOVE, EV_MOUSE_WHEEL };
enum { TR_PRESS, TR_RELEASE, TR_TAP };

typedef struct {
    int kind, transition, port, count;
    const char *names[8];
    int x, y;                      /* EV_MOUSE_MOVE: counts; EV_MOUSE_WHEEL: x unused, y detents */
} remote_event;

/* {"events":[...]}; returns length, or 0 if it doesn't fit in cap. */
size_t remote_events_json(char *out, size_t cap, const remote_event *ev, int n);

/* Key events per request: 24 worst-case events plus joystick, mouse, release_all and the
 * keys pressed again after it stay well under the firmware's 4096-byte body limit. */
#define REMOTE_MAX_KEY_EVENTS 24

#define JOY_MIN_HOLD_MS 60

typedef struct {
    int      port;                 /* port the sent state applies to */
    unsigned sent;                 /* bits the C64 currently holds */
    uint32_t pressed_at[5];        /* ms timestamp per bit */
} joy_track;

/*
 * Compare the wanted state with what the C64 holds and emit at most one
 * event per input: presses at once, releases only after JOY_MIN_HOLD_MS
 * (shorter taps would fall between two frame reads). A port change releases
 * everything on the old port first. Returns events written (max 3).
 */
int joy_plan(joy_track *t, int port, unsigned wanted, uint32_t now_ms, remote_event *out);

/* The same for the mouse buttons (MOUSE_* bits; the port is unused). Max 2 events. */
int mouse_plan(joy_track *t, unsigned wanted, uint32_t now_ms, remote_event *out);

/* One mouse report from the motion added up: at most +-127 counts per axis, taken off
 * *dx and *dy. Returns 1 if it wrote an event, 0 if there was no motion. */
int mouse_motion(int *dx, int *dy, remote_event *out);

/* mouse.pending from a GET or POST machine:input reply, or -1 if it has none. */
int remote_parse_pending(const char *body);
/* The reply has a "mouse" object (the firmware has the mouse). */
int remote_reply_has_mouse(const char *body);

/* A key queued for the C64: a tap (with modifiers), or a press or release of one key. */
typedef struct { int key, mods, transition; } remote_keytap;

typedef struct {
    uint64_t held;                 /* matrix keys the C64 holds (bit = key id) */
    uint32_t pressed_at[64];
} key_track;

/*
 * Keyboard events for the keys queued, in order, until one has to wait: the same key twice
 * in one request, RESTORE (which goes alone), a release less than JOY_MIN_HOLD_MS after its
 * press (*wait_ms says how long), or max events. Taps leave out modifiers already held;
 * presses of held keys and releases of keys not held do nothing. Updates t; returns the
 * events written and sets *taken to the queue entries used.
 */
int keys_plan(key_track *t, const remote_keytap *q, int qlen, uint32_t now_ms,
              remote_event *out, int max, int *taken, int *wait_ms);

/* Press events for every key t holds (after a release_all), max 8 keys each. Returns the
 * events written (at most max). */
int keys_repress(const key_track *t, remote_event *out, int max);

#endif
