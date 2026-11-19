/*
 * controls - maps the game controller (and the Mac keyboard) to C64
 * joystick and key input, the on-screen keyboard and the in-app screens.
 *
 * Normal mode:
 *   D-pad / left stick   joystick (input-command mode, built-in emulator) or cursor
 *                        keys (typing-buffer mode, or joy_keys set)
 *   Cross                fire, or RETURN where the D-pad is cursor keys
 *   Square/Triangle/Circle, L1/R1/L2/R2   configurable shortcuts
 *                        (default SPACE, RETURN, RUN/STOP, F1, F3, F5, F7)
 *   Touchpad             on-screen keyboard
 *   R3                   Ultimate menu button
 *   L3                   file browser
 *   Options              app menu (audio, joystick port, mouse, choose C64, quit)
 * The bound keys are held while their buttons are, where the machine holds keys (the
 * emulator; a C64 Ultimate with the input command), else tapped.
 *
 * Ultimate menu open (C64 Ultimate firmware 1.2.0+ says so): D-pad / left stick are cursor
 * keys, Cross RETURN, Circle RUN/STOP; no joystick.
 *
 * Mouse on (Options > Mouse, Cmd+U; a 1351 on port 1, the joystick then on port 2): the
 * right stick moves it, R1 is its left button and L1 its right. On the Mac, a click on the
 * picture hands it the Mac's mouse (Esc or Cmd lets go).
 *
 * Mac keyboard: in a machine it is the C64's keyboard (keys held as they're held
 * on a built-in machine; typed into the Ultimate's buffer), or with Cmd+J a joystick
 * (arrows, Space or Right Shift fire). PS64's own keys are Cmd+O menu, Cmd+B file
 * browser, Cmd+K on-screen keyboard, Cmd+J typing/joystick, Cmd+U mouse (main: Cmd+F, M,
 * S, N, Q).
 * In PS64's screens arrows, Return, Esc, Space, PageUp/PageDown, Tab, F and L drive
 * them; while typing a search, letters type: Return searches, Tab is Assembly64's
 * category and Cmd+L its latest.
 */
#ifndef CONTROLS_H
#define CONTROLS_H

#include "c64remote.h"
#include "osk.h"
#include "ui.h"

#include <SDL2/SDL.h>

enum { BTN_SQUARE, BTN_TRIANGLE, BTN_CIRCLE, BTN_L1, BTN_R1, BTN_L2, BTN_R2, BTN_BINDABLE };
enum { BIND_NONE, BIND_KEY, BIND_MENU };

typedef struct {
    int kind, key, mods;
} binding;

#define ACTIONQ 8

typedef struct {
    remote   *remote;          /* NULL: no C64 to control */
    ui_state *ui;
    binding   bind[BTN_BINDABLE];
    int       port;            /* joystick port 1 or 2 */
    int       joy_keys;        /* D-pad/stick/Cross as cursor keys and RETURN even with a joystick */
    osk_state osk;

    unsigned  dpad, stick;     /* JOY_UP/DOWN/LEFT/RIGHT */
    int       fire;
    int       ltrig, rtrig;
    unsigned  repeat_dirs;
    Uint32    repeat_at;
    Uint32    updated_at;      /* the last controls_update (a long gap: a stall) */

    ui_action actions[ACTIONQ];
    int       nactions;

    char      banner[96];
    Uint32    banner_until;

    /* The Mac keyboard. */
    int       kbd_joy;         /* arrows and Space / Right Shift are the joystick */
    unsigned  key_dirs;        /* joystick directions held on the keyboard */
    int       key_fire;
    struct { int sc, key, mods; } held[16];   /* C64 keys held down for Mac keys */
    int       nheld;
    int       modrefs[3];      /* held keys needing SHIFT, CBM, CTRL */

    int       quit_held;       /* Circle / Esc pressed on the home screen and still down */

    int       bind_held[BTN_BINDABLE];   /* the button's key is held down on the C64 */
    int       menu_was_open;   /* the Ultimate menu was open at the last update */

    /* The mouse. */
    int       mouse;           /* wanted on (the setting) */
    int       mouse_in;        /* plugged in on the machine (wanted, and it takes one) */
    remote   *mouse_for;       /* the machine mouse_in applies to */
    int       rx, ry;          /* the right stick */
    float     mfrac_x, mfrac_y;
    unsigned  pad_buttons;     /* MOUSE_* held on the controller */
    int       captured;        /* the Mac's mouse is the C64's */
    unsigned  mac_buttons;
} controls;

/* Name -> button index for config keys ("square", "l2", ...); -1 if unknown. */
int controls_button_index(const char *name);
/* Parse a binding value: a c64keys name, "menu" or "none". 0 on success. */
int controls_parse_binding(const char *value, binding *out);

void controls_init(controls *c, remote *r, ui_state *ui, int port);
/* Returns 1 if the event was used, 0 if the caller should handle it. */
int  controls_event(controls *c, const SDL_Event *ev);
void controls_update(controls *c, Uint32 now);
/* Pops the next action produced by the UI (menu choices etc.). */
int  controls_take_action(controls *c, ui_action *out);
void controls_draw(controls *c, SDL_Renderer *ren, Uint32 now);
void controls_banner(controls *c, const char *text);
/* The Mac keyboard as the joystick (on) or the C64's keyboard (off); announce it with a
 * banner. A game starting turns it on, a reset off. */
void controls_keyboard_joystick(controls *c, int on, int announce);
/* The mouse wanted on or off; with announce a banner says how it's used (or that the
 * machine has none). Returns the setting now (off if the machine has no mouse). */
int  controls_set_mouse(controls *c, int on, int announce);
/* A game controller is connected (hints show its buttons; else keyboard keys). */
int  controls_have_pad(void);

#endif
