/*
 * c64remote - forwards keys and joystick state to a C64 Ultimate from a
 * background thread, so the video loop never waits on the network (or, for
 * the built-in emulator, straight to it).
 *
 * At start it probes GET /v1/machine:input:
 *   200      -> REMOTE_INPUT: real key presses and joystick via POST /v1/machine:input
 *   404/501  -> REMOTE_BUFFER: PETSCII typed into the KERNAL keyboard buffer
 *               ($0277, count $C6) with readmem/writemem; no joystick
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

typedef struct remote remote;

remote *remote_start(const char *host, const char *password);

/* A machine in this process (the built-in emulator): taps and joystick
 * state go straight to these callbacks; the mode is REMOTE_INPUT. */
typedef struct {
    void (*key_tap)(int key, int mods);
    void (*joystick)(int port, unsigned bits);
    void (*release_all)(void);
} remote_sink;
remote *remote_start_local(const remote_sink *sink);

void remote_stop(remote *r);               /* releases everything, joins the thread */
int  remote_mode(remote *r);

void remote_key_tap(remote *r, int key, int mods);
void remote_joystick(remote *r, int port, unsigned bits);
void remote_menu_button(remote *r);
void remote_release_all(remote *r);

/* Pops one status message for the UI (e.g. "RESTORE needs newer firmware"). */
int remote_take_message(remote *r, char *out, size_t cap);

/* ---- pure helpers, exposed for unit tests ------------------------------ */

enum { EV_KEYBOARD, EV_JOYSTICK, EV_RELEASE_ALL };
enum { TR_PRESS, TR_RELEASE, TR_TAP };

typedef struct {
    int kind, transition, port, count;
    const char *names[8];
} remote_event;

/* {"events":[...]}; returns length, or 0 if it doesn't fit in cap. */
size_t remote_events_json(char *out, size_t cap, const remote_event *ev, int n);

/* Key events per request: 24 worst-case events plus joystick and release_all
 * stay well under the firmware's 4096-byte body limit. */
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

#endif
