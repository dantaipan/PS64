#include "controls.h"

#include "c64keys.h"
#include "osk_sdl.h"
#include "ui_sdl.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define STICK_ON      16000
#define STICK_OFF     12000
#define TRIGGER_ON    20000
#define TRIGGER_OFF   10000
#define REPEAT_DELAY  400
#define REPEAT_RATE   110
#define STALL_MS      100     /* a longer gap between updates: input wasn't read meanwhile */
#define BANNER_MS     2500
#define MOUSE_DEAD    7000    /* right stick dead zone */
#define MOUSE_SLOW    20.0f   /* mouse counts per second just past it */
#define MOUSE_FAST    400.0f  /* ... and at full tilt */

static const char *const button_names[BTN_BINDABLE] = {
    "square", "triangle", "circle", "l1", "r1", "l2", "r2",
};

int controls_button_index(const char *name)
{
    for (int i = 0; i < BTN_BINDABLE; i++)
        if (!strcasecmp(name, button_names[i]))
            return i;
    return -1;
}

int controls_parse_binding(const char *value, binding *out)
{
    if (!strcasecmp(value, "none")) {
        *out = (binding){ BIND_NONE, 0, 0 };
        return 0;
    }
    if (!strcasecmp(value, "menu")) {
        *out = (binding){ BIND_MENU, 0, 0 };
        return 0;
    }
    int key, mods;
    if (c64key_parse(value, &key, &mods) != 0)
        return -1;
    *out = (binding){ BIND_KEY, key, mods };
    return 0;
}

void controls_init(controls *c, remote *r, ui_state *ui, int port)
{
    memset(c, 0, sizeof(*c));
    c->remote = r;
    c->ui = ui;
    c->port = port;
    osk_init(&c->osk);
    const char *defaults[BTN_BINDABLE] = { "space", "return", "run_stop", "f1", "f3", "f5", "f7" };
    for (int i = 0; i < BTN_BINDABLE; i++)
        controls_parse_binding(defaults[i], &c->bind[i]);
}

void controls_banner(controls *c, const char *text)
{
    snprintf(c->banner, sizeof(c->banner), "%s", text);
    c->banner_until = SDL_GetTicks() + BANNER_MS;
}

static int ui_open(controls *c)
{
    return c->ui && c->ui->screen != UI_CLOSED;
}

/* A screen with a text field taking real-keyboard text right now. */
static int typing(controls *c)
{
    if (!ui_open(c))
        return 0;
    const ui_state *u = c->ui;
    return (u->screen == UI_SEARCH && u->search_focus == 0) ||
           (u->screen == UI_A64 && u->a64_focus == 0);
}

static void push_action(controls *c, ui_action a)
{
    if (a.kind != UA_NONE && c->nactions < ACTIONQ)
        c->actions[c->nactions++] = a;
}

int controls_take_action(controls *c, ui_action *out)
{
    if (!c->nactions)
        return 0;
    *out = c->actions[0];
    memmove(c->actions, c->actions + 1, sizeof(c->actions[0]) * (size_t)--c->nactions);
    return 1;
}

static void to_ui(controls *c, int input)
{
    push_action(c, ui_input(c->ui, input));
}

static void mods_hold(controls *c, int mods, int down);

/* The Ultimate menu is on screen and has the keyboard (PS64's own screens aren't up). */
static int ult_menu(controls *c)
{
    return c->remote && !ui_open(c) && !c->osk.open && remote_menu_open(c->remote);
}

/* The mouse is plugged in and nothing else has the controls. */
static int mouse_active(controls *c)
{
    return c->mouse_in && !ui_open(c) && !c->osk.open && !ult_menu(c);
}

static int mode(controls *c)
{
    int m = c->remote ? remote_mode(c->remote) : REMOTE_UNREACHABLE;
    /* D-pad as cursor keys: drive it like the typing-buffer mode. */
    return m == REMOTE_INPUT && c->joy_keys ? REMOTE_BUFFER : m;
}

static void tap(controls *c, int key, int mods)
{
    if (c->remote)
        remote_key_tap(c->remote, key, mods);
}

static void run_binding(controls *c, const binding *b)
{
    if (!c->remote)
        return;
    if (b->kind == BIND_KEY)
        tap(c, b->key, b->mods);
    else if (b->kind == BIND_MENU)
        remote_menu_button(c->remote);
}

/* A bound button pressed: its key held down while the button is, where the machine holds
 * keys, else typed. */
static void binding_down(controls *c, int btn)
{
    const binding *b = &c->bind[btn];
    if (b->kind == BIND_KEY && remote_holds_keys(c->remote)) {
        if (!c->bind_held[btn]) {
            mods_hold(c, b->mods, 1);
            remote_key(c->remote, b->key, 1);
            c->bind_held[btn] = 1;
        }
        return;
    }
    run_binding(c, b);
}

static void binding_up(controls *c, int btn)
{
    if (!c->bind_held[btn])
        return;
    c->bind_held[btn] = 0;
    remote_key(c->remote, c->bind[btn].key, 0);
    mods_hold(c, c->bind[btn].mods, 0);
}

/* The bindable button a controller button is, or -1. */
static int bind_index(int b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_X:             return BTN_SQUARE;
    case SDL_CONTROLLER_BUTTON_Y:             return BTN_TRIANGLE;
    case SDL_CONTROLLER_BUTTON_B:             return BTN_CIRCLE;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return BTN_L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return BTN_R1;
    }
    return -1;
}

/* Directions for the screens, the OSK cursor, or cursor keys in buffer mode (and in the
 * Ultimate menu). */
static void directions(controls *c, unsigned dirs)
{
    if (ui_open(c)) {
        if (dirs & JOY_UP)    to_ui(c, IN_UP);
        if (dirs & JOY_DOWN)  to_ui(c, IN_DOWN);
        if (dirs & JOY_LEFT)  to_ui(c, IN_LEFT);
        if (dirs & JOY_RIGHT) to_ui(c, IN_RIGHT);
        return;
    }
    int dx = (dirs & JOY_RIGHT ? 1 : 0) - (dirs & JOY_LEFT ? 1 : 0);
    int dy = (dirs & JOY_DOWN ? 1 : 0) - (dirs & JOY_UP ? 1 : 0);
    if (c->osk.open) {
        osk_move(&c->osk, dx, dy);
    } else if (mode(c) == REMOTE_BUFFER || ult_menu(c)) {
        if (dy) tap(c, C64K_CRSR_UD, dy < 0 ? C64MOD_SHIFT : 0);
        if (dx) tap(c, C64K_CRSR_LR, dx < 0 ? C64MOD_SHIFT : 0);
    }
}

static void set_osk(controls *c, int open)
{
    c->osk.open = open;
    c->repeat_dirs = 0;
}

static void toggle_menu(controls *c)
{
    /* On the home screen, Options opens (and closes) its settings instead of the menu. */
    if (c->ui && c->ui->screen == UI_HOME) {
        push_action(c, ui_open_home_settings(c->ui));
        c->repeat_dirs = 0;
        return;
    }
    if (c->ui && c->ui->screen == UI_PICKER && c->ui->picker_from_home) {
        ui_open_home(c->ui);
        c->repeat_dirs = 0;
        return;
    }
    if (ui_open(c))
        ui_close(c->ui);
    else
        ui_open_menu(c->ui);
    c->repeat_dirs = 0;
}

static void osk_type(controls *c)
{
    int key, mods;
    if (osk_press(&c->osk, &key, &mods))
        tap(c, key, mods);
}

/* A digital button press, in game-controller numbering. */
static void button_down(controls *c, int b)
{
    if (b == SDL_CONTROLLER_BUTTON_START) {
        toggle_menu(c);
        return;
    }
    if (ui_open(c)) {
        switch (b) {
        case SDL_CONTROLLER_BUTTON_A:             to_ui(c, IN_OK); break;
        case SDL_CONTROLLER_BUTTON_B:             to_ui(c, IN_BACK); break;
        case SDL_CONTROLLER_BUTTON_X:             to_ui(c, IN_ALT); break;
        case SDL_CONTROLLER_BUTTON_Y:             to_ui(c, IN_TRIANGLE); break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  to_ui(c, IN_L1); break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: to_ui(c, IN_R1); break;
        }
        return;
    }
    if (b == SDL_CONTROLLER_BUTTON_TOUCHPAD) {
        set_osk(c, !c->osk.open);
        return;
    }
    if (c->osk.open) {
        switch (b) {
        case SDL_CONTROLLER_BUTTON_A:             osk_type(c); break;
        case SDL_CONTROLLER_BUTTON_B:             set_osk(c, 0); break;
        case SDL_CONTROLLER_BUTTON_X:             tap(c, C64K_INST_DEL, 0); break;
        case SDL_CONTROLLER_BUTTON_Y:             tap(c, C64K_SPACE, 0); break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  osk_toggle_shift(&c->osk); break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: osk_toggle_cbm(&c->osk); break;
        }
        return;
    }

    /* The Ultimate menu: Cross selects, Circle goes back. */
    if (ult_menu(c) && (b == SDL_CONTROLLER_BUTTON_A || b == SDL_CONTROLLER_BUTTON_B)) {
        tap(c, b == SDL_CONTROLLER_BUTTON_A ? C64K_RETURN : C64K_RUN_STOP, 0);
        return;
    }
    /* The mouse: R1 its left button, L1 its right. */
    if (mouse_active(c) && (b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ||
                            b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
        c->pad_buttons |= b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ? MOUSE_LEFT : MOUSE_RIGHT;
        return;
    }

    switch (b) {
    case SDL_CONTROLLER_BUTTON_A:
        if (mode(c) == REMOTE_INPUT)
            c->fire = 1;
        else
            tap(c, C64K_RETURN, 0);
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        if (c->remote)
            remote_menu_button(c->remote);
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        push_action(c, (ui_action){ .kind = UA_OPEN_BROWSER });
        break;
    default: {
        int i = bind_index(b);
        if (i >= 0)
            binding_down(c, i);
        break;
    }
    }
}

static void button_up(controls *c, int b)
{
    if (b == SDL_CONTROLLER_BUTTON_A)
        c->fire = 0;
    if (b == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)
        c->pad_buttons &= ~MOUSE_LEFT;
    if (b == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)
        c->pad_buttons &= ~MOUSE_RIGHT;
    int i = bind_index(b);
    if (i >= 0)
        binding_up(c, i);
}

static void trigger(controls *c, int right, int value)
{
    int *state = right ? &c->rtrig : &c->ltrig;
    if (!*state && value > TRIGGER_ON) {
        *state = 1;
        if (ui_open(c))
            to_ui(c, right ? IN_R2 : IN_L2);
        else if (c->osk.open) {
            if (right) tap(c, C64K_RETURN, 0);
            else       osk_toggle_ctrl(&c->osk);
        } else {
            binding_down(c, right ? BTN_R2 : BTN_L2);
        }
    } else if (*state && value < TRIGGER_OFF) {
        *state = 0;
        binding_up(c, right ? BTN_R2 : BTN_L2);
    }
}

static void stick(controls *c, int axis, int value)
{
    unsigned neg = axis == SDL_CONTROLLER_AXIS_LEFTX ? JOY_LEFT : JOY_UP;
    unsigned pos = axis == SDL_CONTROLLER_AXIS_LEFTX ? JOY_RIGHT : JOY_DOWN;
    unsigned bits = c->stick;
    if (value < -STICK_ON)       bits = (bits & ~pos) | neg;
    else if (value > STICK_ON)   bits = (bits & ~neg) | pos;
    else if (value > -STICK_OFF && value < STICK_OFF) bits &= ~(neg | pos);
    c->stick = bits;
}

static unsigned dpad_bit(int b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    return JOY_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return JOY_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return JOY_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return JOY_RIGHT;
    }
    return 0;
}

/* ---- Mac keyboard --------------------------------------------------------------- */

void controls_keyboard_joystick(controls *c, int on, int announce)
{
    if (c->kbd_joy == !!on)
        return;
    c->kbd_joy = !!on;
    c->key_dirs = 0;
    c->key_fire = 0;
    if (announce)
        controls_banner(c, on ? "Keyboard: joystick (arrows, Space fires) - Cmd+J types"
                              : "Keyboard: typing - Cmd+J for the joystick");
}

int controls_have_pad(void)
{
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i))
            return 1;
    return 0;
}

/*
 * The C64 key a Mac key is, in place, held while it's held, with the modifiers it needs
 * (F2 is SHIFT+F1, cursor up SHIFT+CRSR_UD); -1 if it's typed as text instead (symbols:
 * they sit on different keys). Digits with Shift are symbols.
 */
static int held_key(SDL_Keycode sym, int shift, int *mods)
{
    int key, m;
    *mods = 0;
    if ((sym >= SDLK_a && sym <= SDLK_z) || sym == SDLK_SPACE || (!shift && sym >= SDLK_0 && sym <= SDLK_9))
        return c64key_from_ascii((int)sym, &key, &m) == 0 ? key : -1;
    switch (sym) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return C64K_RETURN;
    case SDLK_BACKSPACE: return C64K_INST_DEL;
    case SDLK_ESCAPE:    return C64K_RUN_STOP;
    case SDLK_HOME:      return C64K_CLR_HOME;
    case SDLK_PAGEUP:    return C64K_RESTORE;
    case SDLK_RIGHT:     return C64K_CRSR_LR;
    case SDLK_LEFT:      *mods = C64MOD_SHIFT; return C64K_CRSR_LR;
    case SDLK_DOWN:      return C64K_CRSR_UD;
    case SDLK_UP:        *mods = C64MOD_SHIFT; return C64K_CRSR_UD;
    case SDLK_F1:        return C64K_F1;
    case SDLK_F2:        *mods = C64MOD_SHIFT; return C64K_F1;
    case SDLK_F3:        return C64K_F3;
    case SDLK_F4:        *mods = C64MOD_SHIFT; return C64K_F3;
    case SDLK_F5:        return C64K_F5;
    case SDLK_F6:        *mods = C64MOD_SHIFT; return C64K_F5;
    case SDLK_F7:        return C64K_F7;
    case SDLK_F8:        *mods = C64MOD_SHIFT; return C64K_F7;
    }
    return -1;
}

/* The Mac's mouse as the C64's (relative mode: the pointer hidden, motion unbounded). */
static void capture(controls *c, int on)
{
    if (c->captured == !!on)
        return;
    c->captured = !!on;
    c->mac_buttons = 0;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    if (on)
        controls_banner(c, "The Mac's mouse is the C64's - Esc or Cmd lets go");
}

static unsigned mac_button(Uint8 b)
{
    switch (b) {
    case SDL_BUTTON_LEFT:   return MOUSE_LEFT;
    case SDL_BUTTON_RIGHT:  return MOUSE_RIGHT;
    case SDL_BUTTON_MIDDLE: return MOUSE_MIDDLE;
    }
    return 0;
}

static const int mod_keys[3] = { C64K_LSHIFT, C64K_CBM, C64K_CTRL };
static const int mod_bits[3] = { C64MOD_SHIFT, C64MOD_CBM, C64MOD_CTRL };

static void mods_hold(controls *c, int mods, int down)
{
    for (int i = 0; i < 3; i++)
        if (mods & mod_bits[i]) {
            if (down && c->modrefs[i]++ == 0)
                remote_key(c->remote, mod_keys[i], 1);
            else if (!down && c->modrefs[i] > 0 && --c->modrefs[i] == 0)
                remote_key(c->remote, mod_keys[i], 0);
        }
}

/* A C64 key pressed for Mac key sc: held where the machine holds keys, else typed. */
static void press(controls *c, int sc, int key, int mods)
{
    if (!c->remote)
        return;
    if (!remote_holds_keys(c->remote)) {
        remote_key_tap(c->remote, key, mods);
        return;
    }
    for (int i = 0; i < c->nheld; i++)
        if (c->held[i].sc == sc)
            return;                             /* (already down) */
    if (c->nheld == (int)(sizeof(c->held) / sizeof(c->held[0])))
        return;
    c->held[c->nheld].sc = sc;
    c->held[c->nheld].key = key;
    c->held[c->nheld].mods = mods;
    c->nheld++;
    mods_hold(c, mods, 1);
    remote_key(c->remote, key, 1);
}

static void let_go(controls *c, int sc)
{
    for (int i = 0; i < c->nheld; i++)
        if (c->held[i].sc == sc) {
            remote_key(c->remote, c->held[i].key, 0);
            mods_hold(c, c->held[i].mods, 0);
            c->held[i] = c->held[--c->nheld];
            return;
        }
}

static void let_go_all(controls *c)
{
    while (c->nheld)
        let_go(c, c->held[0].sc);
    for (int i = 0; i < BTN_BINDABLE; i++)
        binding_up(c, i);
    c->key_dirs = 0;
    c->key_fire = 0;
    c->pad_buttons = 0;
    c->mac_buttons = 0;
}

static int any_held(controls *c)
{
    for (int i = 0; i < BTN_BINDABLE; i++)
        if (c->bind_held[i])
            return 1;
    return c->nheld || c->key_dirs || c->key_fire || c->pad_buttons || c->mac_buttons;
}

static unsigned arrow_bit(SDL_Keycode sym)
{
    switch (sym) {
    case SDLK_UP:    return JOY_UP;
    case SDLK_DOWN:  return JOY_DOWN;
    case SDLK_LEFT:  return JOY_LEFT;
    case SDLK_RIGHT: return JOY_RIGHT;
    }
    return 0;
}

/* Cmd+key: PS64's own shortcuts. Returns 1 if used (the rest are main's). */
static int command_key(controls *c, SDL_Keycode sym)
{
    switch (sym) {
    case SDLK_o: toggle_menu(c); return 1;
    case SDLK_b:
        if (!ui_open(c))
            push_action(c, (ui_action){ .kind = UA_OPEN_BROWSER });
        return 1;
    case SDLK_k:
        if (!ui_open(c))
            set_osk(c, !c->osk.open);
        return 1;
    case SDLK_j: controls_keyboard_joystick(c, !c->kbd_joy, 1); return 1;
    case SDLK_u: push_action(c, (ui_action){ .kind = UA_MOUSE }); return 1;
    case SDLK_l:                                /* Assembly64's latest, while typing */
        if (typing(c) && c->ui->screen == UI_A64) {
            to_ui(c, IN_R1);
            return 1;
        }
        break;
    }
    return 0;
}

/* Mac keyboard. Returns 1 if used. */
static int key_down(controls *c, const SDL_KeyboardEvent *k)
{
    SDL_Keycode sym = k->keysym.sym;
    int kmod = k->keysym.mod;
    if (c->captured && (sym == SDLK_ESCAPE || sym == SDLK_LGUI || sym == SDLK_RGUI || (kmod & KMOD_GUI))) {
        capture(c, 0);              /* (the Mac's mouse back) */
        if (sym == SDLK_ESCAPE)
            return 1;
    }
    if (kmod & KMOD_GUI) {
        let_go_all(c);
        return command_key(c, sym);
    }
    if (typing(c)) {
        /* Typing a query on a real keyboard: text arrives as SDL_TEXTINPUT,
         * so printable keys don't double as shortcuts here (Assembly64's latest
         * is Cmd+L, its category Tab). */
        switch (sym) {
        case SDLK_RETURN:    to_ui(c, IN_R2); return 1;      /* to the results */
        case SDLK_BACKSPACE: to_ui(c, IN_ALT); return 1;     /* delete */
        case SDLK_ESCAPE:    to_ui(c, IN_BACK); return 1;
        case SDLK_TAB:
            to_ui(c, c->ui->screen == UI_A64 ? IN_L2 : IN_R2);
            return 1;
        }
        if (sym >= SDLK_SPACE && sym <= SDLK_z)
            return 1;
    }
    if (ui_open(c)) {
        let_go_all(c);
        switch (sym) {
        case SDLK_UP:       to_ui(c, IN_UP); break;
        case SDLK_DOWN:     to_ui(c, IN_DOWN); break;
        case SDLK_LEFT:     to_ui(c, IN_LEFT); break;
        case SDLK_RIGHT:    to_ui(c, IN_RIGHT); break;
        case SDLK_RETURN:   to_ui(c, IN_OK); break;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE: to_ui(c, IN_BACK); break;
        case SDLK_SPACE:    to_ui(c, IN_ALT); break;
        case SDLK_PAGEUP:   to_ui(c, IN_L1); break;
        case SDLK_PAGEDOWN: to_ui(c, IN_R1); break;
        case SDLK_TAB:      to_ui(c, IN_R2); break;
        case SDLK_f:        to_ui(c, IN_TRIANGLE); break;      /* favourite */
        case SDLK_l:        to_ui(c, IN_L2); break;
        }
        return 1;
    }
    if (c->osk.open) {
        switch (sym) {
        case SDLK_UP:        osk_move(&c->osk, 0, -1); break;
        case SDLK_DOWN:      osk_move(&c->osk, 0, 1); break;
        case SDLK_LEFT:      osk_move(&c->osk, -1, 0); break;
        case SDLK_RIGHT:     osk_move(&c->osk, 1, 0); break;
        case SDLK_RETURN:    osk_type(c); break;
        case SDLK_SPACE:     tap(c, C64K_SPACE, 0); break;
        case SDLK_BACKSPACE: tap(c, C64K_INST_DEL, 0); break;
        case SDLK_ESCAPE:    set_osk(c, 0); break;
        }
        return 1;
    }
    /* The machine: the joystick keys, else the C64's keyboard. */
    if (c->kbd_joy) {
        unsigned bit = arrow_bit(sym);
        if (bit) {
            c->key_dirs |= bit;
            return 1;
        }
        if (sym == SDLK_SPACE || sym == SDLK_RSHIFT) {
            c->key_fire = 1;
            return 1;
        }
    }
    if (k->repeat)
        return 1;                               /* (the C64 repeats keys itself) */
    int mods, key = held_key(sym, (kmod & KMOD_SHIFT) != 0, &mods);
    if (key < 0)
        return 1;                               /* (typed as text: SDL_TEXTINPUT) */
    if (kmod & KMOD_SHIFT) mods |= C64MOD_SHIFT;
    if (kmod & KMOD_ALT)   mods |= C64MOD_CBM;
    if (kmod & KMOD_CTRL)  mods |= C64MOD_CTRL;
    press(c, k->keysym.scancode, key, mods);
    return 1;
}

static int key_up(controls *c, const SDL_KeyboardEvent *k)
{
    unsigned bit = arrow_bit(k->keysym.sym);
    if (bit)
        c->key_dirs &= ~bit;
    if (k->keysym.sym == SDLK_SPACE || k->keysym.sym == SDLK_RSHIFT)
        c->key_fire = 0;
    let_go(c, k->keysym.scancode);
    return 1;
}

/* Text from the keyboard while a machine has it: the symbols (letters, digits and Space
 * are held keys) typed as the C64 keys that make them. */
static void key_text(controls *c, const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        int key, mods;
        if (*p < 33 || *p > 126 || isalnum(*p))
            continue;
        if (c->kbd_joy && *p == ' ')
            continue;
        if (c64key_from_ascii(*p, &key, &mods) == 0)
            tap(c, key, mods);
    }
}

int controls_event(controls *c, const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_CONTROLLERDEVICEADDED:
        SDL_GameControllerOpen(ev->cdevice.which);
        return 1;
    case SDL_CONTROLLERBUTTONDOWN: {
        if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_B && c->ui && c->ui->screen == UI_HOME)
            c->quit_held = 1;                   /* held, it quits (ui_home_hold) */
        unsigned bit = dpad_bit(ev->cbutton.button);
        if (bit)
            c->dpad |= bit;
        else
            button_down(c, ev->cbutton.button);
        return 1;
    }
    case SDL_CONTROLLERBUTTONUP: {
        if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_B)
            c->quit_held = 0;
        unsigned bit = dpad_bit(ev->cbutton.button);
        if (bit)
            c->dpad &= ~bit;
        else
            button_up(c, ev->cbutton.button);
        return 1;
    }
    case SDL_CONTROLLERAXISMOTION:
        if (ev->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT)
            trigger(c, 0, ev->caxis.value);
        else if (ev->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
            trigger(c, 1, ev->caxis.value);
        else if (ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ||
                 ev->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
            stick(c, ev->caxis.axis, ev->caxis.value);
        else if (ev->caxis.axis == SDL_CONTROLLER_AXIS_RIGHTX)
            c->rx = ev->caxis.value;
        else if (ev->caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY)
            c->ry = ev->caxis.value;
        return 1;
    case SDL_MOUSEBUTTONDOWN:
        if (ev->button.which == SDL_TOUCH_MOUSEID || !mouse_active(c))
            return 0;
        if (!c->captured)
            capture(c, 1);          /* (the click that hands the mouse over isn't sent) */
        else
            c->mac_buttons |= mac_button(ev->button.button);
        return 1;
    case SDL_MOUSEBUTTONUP:
        if (!c->captured)
            return 0;
        c->mac_buttons &= ~mac_button(ev->button.button);
        return 1;
    case SDL_MOUSEMOTION:
        if (!c->captured)
            return 0;
        if (mouse_active(c))
            remote_mouse_move(c->remote, ev->motion.xrel, ev->motion.yrel);
        return 1;
    case SDL_MOUSEWHEEL: {
        if (!c->captured)
            return 0;
        int y = ev->wheel.y;
        if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            y = -y;
        if (mouse_active(c))
            remote_mouse_wheel(c->remote, y);
        return 1;
    }
    case SDL_KEYDOWN:
        if (ev->key.keysym.sym == SDLK_ESCAPE && !ev->key.repeat && c->ui && c->ui->screen == UI_HOME)
            c->quit_held = 1;                   /* (the Mac's Circle) */
        return key_down(c, &ev->key);
    case SDL_KEYUP:
        if (ev->key.keysym.sym == SDLK_ESCAPE)
            c->quit_held = 0;
        return key_up(c, &ev->key);
    case SDL_TEXTINPUT:
        if (typing(c) || (ui_open(c) && c->ui->screen == UI_SEARCH)) {
            ui_text(c->ui, ev->text.text);
            return 1;
        }
        if (!ui_open(c) && !c->osk.open && !(SDL_GetModState() & KMOD_GUI)) {
            key_text(c, ev->text.text);
            return 1;
        }
        return 0;
    }
    return 0;
}

/* Mouse counts from one right stick axis for dt ms: slow just past the dead zone, a
 * quadratic curve up to MOUSE_FAST. */
static int stick_counts(int v, float *frac, Uint32 dt)
{
    int a = v < 0 ? -v : v;
    if (a <= MOUSE_DEAD) {
        *frac = 0;
        return 0;
    }
    float t = (float)(a - MOUSE_DEAD) / (float)(32767 - MOUSE_DEAD);
    if (t > 1)
        t = 1;
    float speed = MOUSE_SLOW + (MOUSE_FAST - MOUSE_SLOW) * t * t;
    *frac += (v < 0 ? -speed : speed) * (float)dt / 1000.0f;
    int n = (int)*frac;
    *frac -= (float)n;
    return n;
}

int controls_set_mouse(controls *c, int on, int announce)
{
    int detecting = c->remote && remote_mode(c->remote) == REMOTE_DETECTING;
    if (on && !(c->remote && remote_has_mouse(c->remote)) && !detecting) {
        if (announce)
            controls_banner(c, !c->remote || remote_local(c->remote)
                               ? "This machine has no mouse port"
                               : "The mouse needs C64 Ultimate firmware 1.2.1");
        c->mouse = 0;
        return 0;
    }
    c->mouse = !!on;
    if (announce) {
        char msg[96];
        if (!on)
            snprintf(msg, sizeof(msg), "Mouse off");
        else if (controls_have_pad())
            snprintf(msg, sizeof(msg), "Mouse on port 1: right stick moves it, R1/L1 click%s",
                     c->port == 1 ? " (joystick on port 2)" : "");
        else
            snprintf(msg, sizeof(msg), "Mouse on port 1: click the picture to use the Mac's mouse");
        controls_banner(c, msg);
    }
    return c->mouse;
}

/* Plug the mouse in or out to match the setting, move it with the right stick, press its
 * buttons. */
static void update_mouse(controls *c, Uint32 dt)
{
    if (c->mouse_for != c->remote) {           /* (another machine: nothing plugged in yet) */
        c->mouse_for = c->remote;
        c->mouse_in = 0;
    }
    int in = c->mouse && c->remote && remote_has_mouse(c->remote);
    if (in != c->mouse_in) {
        c->mouse_in = in;
        remote_mouse_enable(c->remote, in);
        c->mfrac_x = c->mfrac_y = 0;
        c->pad_buttons = 0;
        if (!in)
            capture(c, 0);
    }
    if (!c->mouse_in)
        return;
    int active = mouse_active(c);
    if (!active) {
        c->pad_buttons = 0;
        capture(c, 0);
    } else {
        int dx = stick_counts(c->rx, &c->mfrac_x, dt);
        int dy = stick_counts(c->ry, &c->mfrac_y, dt);
        remote_mouse_move(c->remote, dx, dy);
    }
    remote_mouse_buttons(c->remote, active ? c->pad_buttons | c->mac_buttons : 0);
}

void controls_update(controls *c, Uint32 now)
{
    /* Circle held on the home screen: quit once it's been held long enough. */
    if (c->ui) {
        if (c->ui->screen != UI_HOME)
            c->quit_held = 0;
        push_action(c, ui_home_hold(c->ui, c->quit_held, now));
    }
    if ((ui_open(c) || c->osk.open) && any_held(c))
        let_go_all(c);                          /* (a screen opened while keys were down) */
    unsigned dirs = c->dpad | c->stick | c->key_dirs;
    int menu = ult_menu(c);
    if (menu && !c->menu_was_open)
        controls_banner(c, controls_have_pad() ? "Ultimate menu: D-pad moves, Cross selects, Circle goes back"
                                               : "Ultimate menu: arrows move, Return selects, Esc goes back");
    c->menu_was_open = menu;
    int joystick = !ui_open(c) && !c->osk.open && !menu && mode(c) == REMOTE_INPUT;
    Uint32 dt = c->updated_at ? now - c->updated_at : 0;
    update_mouse(c, dt > 50 ? 50 : dt);

    /* Edge-triggered with auto-repeat for screens, the OSK cursor and cursor keys. After a
     * stall (the emulator fitting a 16 MB REU, say) a press may have ended unseen, so a held
     * direction waits for input read since before it repeats. */
    if (c->updated_at && now - c->updated_at > STALL_MS && SDL_TICKS_PASSED(now, c->repeat_at))
        c->repeat_at = now + REPEAT_RATE;
    c->updated_at = now;
    if (joystick) {
        c->repeat_dirs = dirs;
    } else if (dirs != c->repeat_dirs) {
        unsigned fresh = dirs & ~c->repeat_dirs;
        if (fresh)
            directions(c, fresh);
        c->repeat_dirs = dirs;
        c->repeat_at = now + REPEAT_DELAY;
    } else if (dirs && SDL_TICKS_PASSED(now, c->repeat_at)) {
        directions(c, dirs);
        c->repeat_at = now + REPEAT_RATE;
    }

    /* The mouse is on port 1, so the joystick goes to port 2 meanwhile. */
    int port = c->mouse_in && c->port == 1 ? 2 : c->port;
    if (c->remote)
        remote_joystick(c->remote, port,
                        joystick ? dirs | (c->fire || c->key_fire ? JOY_FIRE : 0) : 0);
}

void controls_draw(controls *c, SDL_Renderer *ren, Uint32 now)
{
    if (c->osk.open && !ui_open(c))
        osk_draw(ren, &c->osk, 1);
    if (c->ui)
        ui_draw(ren, c->ui, now);
    if (c->banner[0] && !SDL_TICKS_PASSED(now, c->banner_until))
        osk_draw_banner(ren, c->banner);
}
