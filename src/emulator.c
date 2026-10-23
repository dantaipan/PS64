#include "emulator.h"

#include "c64keys.h"
#include "c64remote.h"

#include "libretro.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* VICE internals (the core is linked statically, so these are reachable). */
void reload_restart(void);
void keyboard_set_keyarr(int row, int col, int value);
void keyboard_clear_keymatrix(void);
void machine_set_restore_key(int v);
void machine_trigger_reset(unsigned int mode);
int  file_system_attach_disk(unsigned int unit, unsigned int drive, const char *filename);
int  cartridge_attach_image(int type, const char *filename);
void cartridge_detach_image(int type);
#define VICE_CARTRIDGE_CRT 0              /* a .crt file: its header says the type */
#define VICE_RESET_CPU    0
#define VICE_POWER_CYCLE  1

/* ---- state ---------------------------------------------------------------------- */

static int running;
static char sys_dir[512], save_dir[512];
static enum retro_pixel_format pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static uint32_t frame[EMULATOR_MAX_W * EMULATOR_MAX_H];
static int frame_w, frame_h, have_frame;
static struct retro_system_av_info av;
static int fast;
static unsigned joy_bits;
static int joy_port = 2;
static char cart_path[512];               /* the cartridge kept in the slot ("" none) */
static void attach_cart(void);

/* Audio between frames: a frame is ~960 stereo frames; keep a few. */
#define AUDIO_CAP 16384
static int16_t audio[AUDIO_CAP * 2];
static size_t audio_len;

#define MSG_MAX 4
static char messages[MSG_MAX][96];
static int nmessages;

static struct retro_disk_control_callback disk_cb;
static int have_disk_cb;

/*
 * Core options. The core declares them (legacy SET_VARIABLES, first value is
 * the default); PS64 overrides a few.
 */
#define MAX_VARS 400
static char var_keys[MAX_VARS][48], var_vals[MAX_VARS][48];
static int nvars, vars_changed;

static const char *const overrides[][2] = {
    { "vice_autoloadwarp", "enabled" },          /* load disks and tapes at full speed */
    { "vice_sound_sample_rate", "48000" },
    { "vice_statusbar", "disabled" },
    { "vice_read_vicerc", "disabled" },
    { "vice_work_disk", "disabled" },
    { "vice_retropad_options", "disabled" },     /* B = fire */
    { "vice_physical_keyboard_pass_through", "disabled" },
};

static void set_var(const char *key, const char *value)
{
    for (int i = 0; i < nvars; i++)
        if (!strcmp(var_keys[i], key)) {
            if (strcmp(var_vals[i], value)) {
                snprintf(var_vals[i], sizeof(var_vals[0]), "%s", value);
                vars_changed = 1;
            }
            return;
        }
    if (nvars < MAX_VARS) {
        snprintf(var_keys[nvars], sizeof(var_keys[0]), "%s", key);
        snprintf(var_vals[nvars], sizeof(var_vals[0]), "%s", value);
        nvars++;
        vars_changed = 1;
    }
}

/* ---- libretro callbacks ------------------------------------------------------- */

#ifdef PS64_NATIVE
#define LOG_LEVEL RETRO_LOG_INFO          /* the native app's log is the only window into it */
#else
#define LOG_LEVEL RETRO_LOG_WARN
#endif

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
    if (level < LOG_LEVEL)
        return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "vice: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void add_message(const char *text)
{
    if (nmessages == MSG_MAX) {
        memmove(messages, messages + 1, sizeof(messages[0]) * (MSG_MAX - 1));
        nmessages--;
    }
    snprintf(messages[nmessages++], sizeof(messages[0]), "%s", text);
}

static bool environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = sys_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = save_dir;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format f = *(const enum retro_pixel_format *)data;
        if (f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_RGB565 &&
            f != RETRO_PIXEL_FORMAT_0RGB1555)
            return false;
        pixfmt = f;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = log_cb;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        if (data)
            *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 0;                   /* legacy SET_VARIABLES */
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        for (const struct retro_variable *v = data; v->key; v++) {
            const char *semi = strchr(v->value, ';');
            const char *val = semi ? semi + 1 : v->value;
            while (*val == ' ')
                val++;
            char def[48];
            snprintf(def, sizeof(def), "%.*s", (int)strcspn(val, "|"), val);
            set_var(v->key, def);
        }
        for (size_t i = 0; i < sizeof(overrides) / sizeof(overrides[0]); i++)
            set_var(overrides[i][0], overrides[i][1]);
        set_var("vice_joyport", joy_port == 1 ? "1" : "2");
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        v->value = NULL;
        for (int i = 0; i < nvars; i++)
            if (!strcmp(var_keys[i], v->key)) {
                v->value = var_vals[i];
                return true;
            }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = vars_changed;
        vars_changed = 0;
        return true;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        return true;                             /* keys go into the matrix directly */
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        *(unsigned *)data = 0;                   /* the basic interface is enough */
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        disk_cb = *(const struct retro_disk_control_callback *)data;
        have_disk_cb = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
        if (data)
            fast = ((const struct retro_fastforwarding_override *)data)->fastforward;
        return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        av = *(const struct retro_system_av_info *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        av.geometry = *(const struct retro_game_geometry *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: {
        const struct retro_message *m = data;
        if (m && m->msg && *m->msg)
            add_message(m->msg);
        return true;
    }
    default:
        return false;
    }
}

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data)
        return;                                  /* duplicate of the last frame */
    if (w > EMULATOR_MAX_W) w = EMULATOR_MAX_W;
    if (h > EMULATOR_MAX_H) h = EMULATOR_MAX_H;
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *row = (const uint8_t *)data + y * pitch;
        uint32_t *out = frame + y * w;
        if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
            const uint32_t *p = (const uint32_t *)row;
            for (unsigned x = 0; x < w; x++)
                out[x] = 0xFF000000u | (p[x] & 0xFFFFFF);
        } else {
            const uint16_t *p = (const uint16_t *)row;
            for (unsigned x = 0; x < w; x++) {
                unsigned r, g, b, v = p[x];
                if (pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
                    r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31;
                    r = r << 3 | r >> 2; g = g << 2 | g >> 4; b = b << 3 | b >> 2;
                } else {
                    r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31;
                    r = r << 3 | r >> 2; g = g << 3 | g >> 2; b = b << 3 | b >> 2;
                }
                out[x] = 0xFF000000u | r << 16 | g << 8 | b;
            }
        }
    }
    frame_w = (int)w;
    frame_h = (int)h;
    have_frame = 1;
}

static size_t audio_batch(const int16_t *d, size_t frames)
{
    size_t room = AUDIO_CAP - audio_len, n = frames < room ? frames : room;
    memcpy(audio + audio_len * 2, d, n * 4);
    audio_len += n;
    return frames;
}

static void audio_sample(int16_t l, int16_t r)
{
    int16_t s[2] = { l, r };
    audio_batch(s, 1);
}

static void input_poll(void) {}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port != 0 || device != RETRO_DEVICE_JOYPAD)
        return 0;
    unsigned mask = 0;
    if (joy_bits & JOY_UP)    mask |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
    if (joy_bits & JOY_DOWN)  mask |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (joy_bits & JOY_LEFT)  mask |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (joy_bits & JOY_RIGHT) mask |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
    if (joy_bits & JOY_FIRE)  mask |= 1u << RETRO_DEVICE_ID_JOYPAD_B;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        return (int16_t)mask;
    return id < 16 ? (mask >> id) & 1 : 0;
}

/* ---- keys ------------------------------------------------------------------------ */

/*
 * Taps are played out over emulated frames: modifiers down, key down, held
 * for a few frames (the KERNAL and games scan once per frame), key up,
 * modifiers up, a gap.
 */
#define TAPQ 64
static struct { int key, mods; } taps[TAPQ];
static int ntaps, tap_step;

static void set_key(int key, int down)
{
    if (key == C64K_RESTORE)
        machine_set_restore_key(down);
    else if (key >= 0 && key < 64)
        keyboard_set_keyarr(key >> 3, key & 7, down);
}

static void set_mods(int mods, int down)
{
    if (mods & C64MOD_SHIFT) set_key(C64K_LSHIFT, down);
    if (mods & C64MOD_CBM)   set_key(C64K_CBM, down);
    if (mods & C64MOD_CTRL)  set_key(C64K_CTRL, down);
}

static void step_taps(void)
{
    if (!ntaps)
        return;
    int key = taps[0].key, mods = taps[0].mods;
    switch (tap_step) {
    case 0: set_mods(mods, 1); break;
    case 1: set_key(key, 1); break;
    case 4: set_key(key, 0); break;
    case 5: set_mods(mods, 0); break;
    }
    if (++tap_step == 7) {
        memmove(taps, taps + 1, sizeof(taps[0]) * (size_t)--ntaps);
        tap_step = 0;
    }
}

void emulator_key(int key, int down)
{
    if (running)
        set_key(key, down);
}

void emulator_key_tap(int key, int mods)
{
    if (running && ntaps < TAPQ) {
        taps[ntaps].key = key;
        taps[ntaps].mods = mods;
        ntaps++;
    }
}

void emulator_release_keys(void)
{
    ntaps = tap_step = 0;
    joy_bits = 0;
    if (running) {
        keyboard_clear_keymatrix();
        machine_set_restore_key(0);
    }
}

void emulator_joystick(int port, unsigned bits)
{
    if (port != joy_port && (port == 1 || port == 2)) {
        joy_port = port;
        set_var("vice_joyport", port == 1 ? "1" : "2");
    }
    joy_bits = bits;
}

/* ---- lifecycle ------------------------------------------------------------------- */

static void make_dir(const char *path)
{
    char p[512];
    snprintf(p, sizeof(p), "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0755);
            *s = '/';
        }
    mkdir(p, 0755);
}

int emulator_start(const char *dir, int joystick_port)
{
    /* The core can't be set up twice in one process: starting again is a
     * fresh power-on of the same machine. */
    if (running) {
        emulator_joystick(joystick_port, 0);
        emulator_reset(1);
        return 0;
    }
    snprintf(sys_dir, sizeof(sys_dir), "%s/system", dir);
    snprintf(save_dir, sizeof(save_dir), "%s/saves", dir);
    make_dir(sys_dir);
    make_dir(save_dir);
    joy_port = joystick_port == 1 ? 1 : 2;
    nvars = 0;

    retro_set_environment(environment);
    retro_set_video_refresh(video_refresh);
    retro_set_audio_sample(audio_sample);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    printf("emulator: starting the core\n");
    retro_init();
    printf("emulator: booting\n");
    if (!retro_load_game(NULL)) {
        retro_deinit();
        return -1;
    }
    retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    retro_get_system_av_info(&av);
    /* The core finishes setting up in its first frame; loading before that
     * would set it up a second time (and crash). */
    printf("emulator: first frame\n");
    retro_run();
    running = 1;
    attach_cart();
    printf("emulator: %ux%u, %.3f fps, audio %.0f Hz\n", av.geometry.base_width,
           av.geometry.base_height, av.timing.fps, av.timing.sample_rate);
    return 0;
}

void emulator_stop(void)
{
    if (!running)
        return;
    retro_unload_game();
    retro_deinit();
    running = 0;
    have_frame = 0;
    audio_len = 0;
    ntaps = tap_step = 0;
    have_disk_cb = 0;
}

int emulator_running(void) { return running; }

void emulator_run_frame(void)
{
    if (!running)
        return;
    step_taps();
    retro_run();
}

double emulator_fps(void)       { return av.timing.fps > 0 ? av.timing.fps : 50.125; }
int    emulator_sample_rate(void) { return av.timing.sample_rate > 0 ? (int)av.timing.sample_rate : 48000; }
int    emulator_fast(void)      { return fast; }

const uint32_t *emulator_frame(int *w, int *h)
{
    if (!have_frame)
        return NULL;
    *w = frame_w;
    *h = frame_h;
    return frame;
}

size_t emulator_take_audio(int16_t *out, size_t max_frames)
{
    size_t n = audio_len < max_frames ? audio_len : max_frames;
    memcpy(out, audio, n * 4);
    memmove(audio, audio + n * 2, (audio_len - n) * 4);
    audio_len -= n;
    return n;
}

int emulator_take_message(char *out, size_t cap)
{
    if (!nmessages)
        return 0;
    snprintf(out, cap, "%s", messages[0]);
    memmove(messages, messages + 1, sizeof(messages[0]) * (size_t)--nmessages);
    return 1;
}

/* ---- content --------------------------------------------------------------------- */

/* Put the chosen cartridge back (the core takes everything out on each load). VICE
 * switches the C64 off and on around a cartridge change. */
static void attach_cart(void)
{
    if (cart_path[0] && cartridge_attach_image(VICE_CARTRIDGE_CRT, cart_path) != 0)
        fprintf(stderr, "emulator: can't attach the cartridge %s\n", cart_path);
}

static int is_crt(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot && !strcasecmp(dot, ".crt");
}

int emulator_set_cartridge(const char *path)
{
    snprintf(cart_path, sizeof(cart_path), "%s", path ? path : "");
    if (!running)
        return 0;
    emulator_release_keys();
    cartridge_detach_image(-1);
    if (!cart_path[0]) {
        machine_trigger_reset(VICE_POWER_CYCLE);
        return 0;
    }
    if (cartridge_attach_image(VICE_CARTRIDGE_CRT, cart_path) != 0) {
        fprintf(stderr, "emulator: can't attach the cartridge %s\n", cart_path);
        cart_path[0] = 0;
        machine_trigger_reset(VICE_POWER_CYCLE);
        return -1;
    }
    return 0;
}

int emulator_load(const char *path)
{
    if (!running)
        return -1;
    emulator_release_keys();
    retro_unload_game();
    struct retro_game_info g = { path, NULL, 0, NULL };
    if (!retro_load_game(&g))
        return -1;
    /* A load into a running core is otherwise only a plain reset; this
     * rebuilds the autostart command line and starts the file. */
    reload_restart();
    if (!is_crt(path))
        attach_cart();                       /* (a cartridge game takes the slot) */
    return 0;
}

int emulator_insert_disk(const char *path)
{
    if (!running)
        return -1;
    return file_system_attach_disk(8, 0, path) == 0 ? 0 : -1;
}

void emulator_reset(int power_cycle)
{
    if (!running)
        return;
    emulator_release_keys();
    if (power_cycle) {
        retro_unload_game();                 /* detach disks, tapes and cartridges */
        attach_cart();                       /* ...but the chosen cartridge stays in */
    }
    machine_trigger_reset(power_cycle ? VICE_POWER_CYCLE : VICE_RESET_CPU);
}

int emulator_disk_count(void)
{
    return running && have_disk_cb && disk_cb.get_num_images ? (int)disk_cb.get_num_images() : 0;
}

int emulator_disk_index(void)
{
    return running && have_disk_cb && disk_cb.get_image_index ? (int)disk_cb.get_image_index() : 0;
}

int emulator_disk_insert(int index)
{
    if (index < 0 || index >= emulator_disk_count())
        return -1;
    disk_cb.set_eject_state(true);
    bool ok = disk_cb.set_image_index((unsigned)index);
    disk_cb.set_eject_state(false);
    return ok ? 0 : -1;
}
