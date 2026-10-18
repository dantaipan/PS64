#include "controls.h"

#include "c64keys.h"
#include "osk_sdl.h"
#include "ui_sdl.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define STICK_ON      16000
#define STICK_OFF     12000
#define TRIGGER_ON    20000
#define TRIGGER_OFF   10000
#define REPEAT_DELAY  400
#define REPEAT_RATE   110
#define BANNER_MS     2500

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
           (u->screen == UI_A64 && u->a64_focus == 0) || u->screen == UI_A64_ID;
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

/* Directions for the screens, the OSK cursor, or cursor keys in buffer mode. */
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
    } else if (mode(c) == REMOTE_BUFFER) {
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

    switch (b) {
    case SDL_CONTROLLER_BUTTON_A:
        if (mode(c) == REMOTE_INPUT)
            c->fire = 1;
        else
            tap(c, C64K_RETURN, 0);
        break;
    case SDL_CONTROLLER_BUTTON_X:             run_binding(c, &c->bind[BTN_SQUARE]); break;
    case SDL_CONTROLLER_BUTTON_Y:             run_binding(c, &c->bind[BTN_TRIANGLE]); break;
    case SDL_CONTROLLER_BUTTON_B:             run_binding(c, &c->bind[BTN_CIRCLE]); break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  run_binding(c, &c->bind[BTN_L1]); break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: run_binding(c, &c->bind[BTN_R1]); break;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        if (c->remote)
            remote_menu_button(c->remote);
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        push_action(c, (ui_action){ .kind = UA_OPEN_BROWSER });
        break;
    }
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
            run_binding(c, &c->bind[right ? BTN_R2 : BTN_L2]);
        }
    } else if (*state && value < TRIGGER_OFF) {
        *state = 0;
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

/* Mac keyboard. Returns 1 if used. */
static int key_down(controls *c, SDL_Keycode sym)
{
    if (typing(c)) {
        /* Typing a query on a real keyboard: text arrives as SDL_TEXTINPUT,
         * so printable keys don't double as shortcuts here. */
        switch (sym) {
        case SDLK_RETURN:    to_ui(c, IN_R2); return 1;      /* to the results */
        case SDLK_BACKSPACE: to_ui(c, IN_ALT); return 1;     /* delete */
        case SDLK_ESCAPE:    to_ui(c, IN_BACK); return 1;
        }
        if (sym >= SDLK_SPACE && sym <= SDLK_z)
            return 1;
    }
    if (sym == SDLK_o) {
        toggle_menu(c);
        return 1;
    }
    if (ui_open(c)) {
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
    if (sym == SDLK_b) {
        push_action(c, (ui_action){ .kind = UA_OPEN_BROWSER });
        return 1;
    }
    if (sym == SDLK_TAB) {
        set_osk(c, !c->osk.open);
        return 1;
    }
    if (!c->osk.open)
        return 0;
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

int controls_event(controls *c, const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_CONTROLLERDEVICEADDED:
        SDL_GameControllerOpen(ev->cdevice.which);
        return 1;
    case SDL_CONTROLLERBUTTONDOWN: {
        unsigned bit = dpad_bit(ev->cbutton.button);
        if (bit)
            c->dpad |= bit;
        else
            button_down(c, ev->cbutton.button);
        return 1;
    }
    case SDL_CONTROLLERBUTTONUP: {
        unsigned bit = dpad_bit(ev->cbutton.button);
        if (bit)
            c->dpad &= ~bit;
        else if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_A)
            c->fire = 0;
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
        return 1;
    case SDL_KEYDOWN:
        return key_down(c, ev->key.keysym.sym);
    case SDL_TEXTINPUT:
        if (typing(c) || (ui_open(c) && c->ui->screen == UI_SEARCH)) {
            ui_text(c->ui, ev->text.text);
            return 1;
        }
        return 0;
    }
    return 0;
}

void controls_update(controls *c, Uint32 now)
{
    unsigned dirs = c->dpad | c->stick;
    int joystick = !ui_open(c) && !c->osk.open && mode(c) == REMOTE_INPUT;

    /* Edge-triggered with auto-repeat for screens, the OSK cursor and cursor keys. */
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

    if (c->remote)
        remote_joystick(c->remote, c->port, joystick ? dirs | (c->fire ? JOY_FIRE : 0) : 0);
}

void controls_draw(controls *c, SDL_Renderer *ren, Uint32 now)
{
    if (c->osk.open && !ui_open(c))
        osk_draw(ren, &c->osk);
    if (c->ui)
        ui_draw(ren, c->ui, now);
    if (c->banner[0] && !SDL_TICKS_PASSED(now, c->banner_until))
        osk_draw_banner(ren, c->banner);
}
