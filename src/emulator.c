#include "emulator.h"

#include "c64keys.h"
#include "c64remote.h"
#include "vic20keys.h"

#include "libretro.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/*
 * The cores PS64 links, both statically: the C64 (x64sc) and, with PS64_VIC20, the VIC-20
 * (xvic). The VIC-20 core's entry points and the VICE functions below are renamed vic20_*
 * (tools/vice_vic20_names.h) and the rest of it is local, so the two sit side by side.
 */
#define VICE_API(p) \
    void p##retro_set_environment(retro_environment_t); \
    void p##retro_set_video_refresh(retro_video_refresh_t); \
    void p##retro_set_audio_sample(retro_audio_sample_t); \
    void p##retro_set_audio_sample_batch(retro_audio_sample_batch_t); \
    void p##retro_set_input_poll(retro_input_poll_t); \
    void p##retro_set_input_state(retro_input_state_t); \
    void p##retro_init(void); \
    void p##retro_deinit(void); \
    bool p##retro_load_game(const struct retro_game_info *); \
    void p##retro_unload_game(void); \
    void p##retro_run(void); \
    void p##retro_get_system_av_info(struct retro_system_av_info *); \
    void p##retro_set_controller_port_device(unsigned, unsigned); \
    void p##reload_restart(void); \
    void p##keyboard_set_keyarr(int row, int col, int value); \
    void p##keyboard_clear_keymatrix(void); \
    void p##machine_set_restore_key(int v); \
    void p##machine_trigger_reset(unsigned int mode); \
    int  p##file_system_attach_disk(unsigned int unit, unsigned int drive, const char *filename); \
    int  p##cartridge_attach_image(int type, const char *filename); \
    void p##cartridge_detach_image(int type);
VICE_API()
#ifdef PS64_VIC20
VICE_API(vic20_)
#endif

typedef struct {
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    void (*init)(void);
    void (*deinit)(void);
    bool (*load_game)(const struct retro_game_info *);
    void (*unload_game)(void);
    void (*run)(void);
    void (*get_system_av_info)(struct retro_system_av_info *);
    void (*set_controller_port_device)(unsigned, unsigned);
    void (*reload_restart)(void);
    void (*keyboard_set_keyarr)(int, int, int);
    void (*keyboard_clear_keymatrix)(void);
    void (*machine_set_restore_key)(int);
    void (*machine_trigger_reset)(unsigned int);
    int  (*file_system_attach_disk)(unsigned int, unsigned int, const char *);
    int  (*cartridge_attach_image)(int, const char *);
    void (*cartridge_detach_image)(int);
} core_api;

#define CORE_API(p) { \
    p##retro_set_environment, p##retro_set_video_refresh, p##retro_set_audio_sample, \
    p##retro_set_audio_sample_batch, p##retro_set_input_poll, p##retro_set_input_state, \
    p##retro_init, p##retro_deinit, p##retro_load_game, p##retro_unload_game, p##retro_run, \
    p##retro_get_system_av_info, p##retro_set_controller_port_device, p##reload_restart, \
    p##keyboard_set_keyarr, p##keyboard_clear_keymatrix, p##machine_set_restore_key, \
    p##machine_trigger_reset, p##file_system_attach_disk, p##cartridge_attach_image, \
    p##cartridge_detach_image }
static const core_api cores[EMU_MACHINES] = {
    CORE_API(),
#ifdef PS64_VIC20
    CORE_API(vic20_),
#endif
};

#define VICE_CARTRIDGE_CRT 0              /* a .crt file: its header says the type */
#define VICE_RESET_CPU    0
#define VICE_POWER_CYCLE  1

/* ---- state ---------------------------------------------------------------------- */

/*
 * Core options. The core declares them (legacy SET_VARIABLES, first value is
 * the default); PS64 overrides a few.
 */
#define MAX_VARS 400

/* Each machine's own: a core keeps running (paused) while the other one is used, since a
 * core can only be set up once per process. */
typedef struct {
    const core_api *api;
    int running;
    char sys_dir[512], save_dir[512];
    enum retro_pixel_format pixfmt;
    struct retro_system_av_info av;
    int fast;
    int joy_port;
    char cart_path[512];                  /* the cartridge kept in the slot ("" none) */
    struct retro_disk_control_callback disk_cb;
    int have_disk_cb;
    char var_keys[MAX_VARS][48], var_vals[MAX_VARS][48];
    int nvars, vars_changed;
} machine;

#ifdef PS64_VIC20
#define ON_VIC20 (cur_id == EMU_VIC20)
#else
#define ON_VIC20 0
#endif

static machine machines[EMU_MACHINES];
static int cur_id = EMU_C64;
static machine *cur = &machines[EMU_C64];  /* the one shown, and the one the callbacks serve */

/* The picture, sound, messages and keys of the machine shown. */
static uint32_t frame[EMULATOR_MAX_W * EMULATOR_MAX_H];
static int frame_w, frame_h, have_frame;
static unsigned joy_bits;
static void attach_cart(void);

/* Audio between frames: a frame is ~960 stereo frames; keep a few. */
#define AUDIO_CAP 16384
static int16_t audio[AUDIO_CAP * 2];
static size_t audio_len;

#define MSG_MAX 4
static char messages[MSG_MAX][96];
static int nmessages;

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
    for (int i = 0; i < cur->nvars; i++)
        if (!strcmp(cur->var_keys[i], key)) {
            if (strcmp(cur->var_vals[i], value)) {
                snprintf(cur->var_vals[i], sizeof(cur->var_vals[0]), "%s", value);
                cur->vars_changed = 1;
            }
            return;
        }
    if (cur->nvars < MAX_VARS) {
        snprintf(cur->var_keys[cur->nvars], sizeof(cur->var_keys[0]), "%s", key);
        snprintf(cur->var_vals[cur->nvars], sizeof(cur->var_vals[0]), "%s", value);
        cur->nvars++;
        cur->vars_changed = 1;
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
        *(const char **)data = cur->sys_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = cur->save_dir;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format f = *(const enum retro_pixel_format *)data;
        if (f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_RGB565 &&
            f != RETRO_PIXEL_FORMAT_0RGB1555)
            return false;
        cur->pixfmt = f;
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
        if (ON_VIC20)                            /* RAM expansion from the program's size and */
            set_var("vice_vic20_memory_expansions", "auto");   /* (8k)-style filename tags */
        else
            set_var("vice_joyport", cur->joy_port == 1 ? "1" : "2");   /* (the VIC-20 has one) */
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        v->value = NULL;
        for (int i = 0; i < cur->nvars; i++)
            if (!strcmp(cur->var_keys[i], v->key)) {
                v->value = cur->var_vals[i];
                return true;
            }
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = cur->vars_changed;
        cur->vars_changed = 0;
        return true;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        return true;                             /* keys go into the matrix directly */
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
        *(unsigned *)data = 0;                   /* the basic interface is enough */
        return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
        cur->disk_cb = *(const struct retro_disk_control_callback *)data;
        cur->have_disk_cb = 1;
        return true;
    case RETRO_ENVIRONMENT_SET_FASTFORWARDING_OVERRIDE:
        if (data)
            cur->fast = ((const struct retro_fastforwarding_override *)data)->fastforward;
        return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        cur->av = *(const struct retro_system_av_info *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        cur->av.geometry = *(const struct retro_game_geometry *)data;
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
        if (cur->pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
            const uint32_t *p = (const uint32_t *)row;
            for (unsigned x = 0; x < w; x++)
                out[x] = 0xFF000000u | (p[x] & 0xFFFFFF);
        } else {
            const uint16_t *p = (const uint16_t *)row;
            for (unsigned x = 0; x < w; x++) {
                unsigned r, g, b, v = p[x];
                if (cur->pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
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
        cur->api->machine_set_restore_key(down);
    else if (key >= 0 && key < 64) {
        int k = ON_VIC20 ? vic20_key(key) : key;     /* (its own matrix) */
        cur->api->keyboard_set_keyarr(k >> 3, k & 7, down);
    }
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
    if (cur->running)
        set_key(key, down);
}

void emulator_key_tap(int key, int mods)
{
    if (cur->running && ntaps < TAPQ) {
        taps[ntaps].key = key;
        taps[ntaps].mods = mods;
        ntaps++;
    }
}

void emulator_release_keys(void)
{
    ntaps = tap_step = 0;
    joy_bits = 0;
    if (cur->running) {
        cur->api->keyboard_clear_keymatrix();
        cur->api->machine_set_restore_key(0);
    }
}

void emulator_joystick(int port, unsigned bits)
{
    if (cur_id == EMU_C64 && port != cur->joy_port && (port == 1 || port == 2)) {
        cur->joy_port = port;
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

static const char *const machine_names[EMU_MACHINES] = {
    "C64",
#ifdef PS64_VIC20
    "VIC-20",
#endif
};

int emulator_select(int which)
{
    if (which < 0 || which >= EMU_MACHINES)
        return -1;
    if (which == cur_id)
        return 0;
    emulator_release_keys();                 /* (nothing held down while it waits) */
    cur_id = which;
    cur = &machines[which];
    have_frame = 0;                          /* the other machine's picture and sound go */
    audio_len = 0;
    nmessages = 0;
    printf("emulator: %s\n", machine_names[which]);
    return 0;
}

int emulator_machine(void) { return cur_id; }

int emulator_start(const char *dir, int joystick_port)
{
    cur->api = &cores[cur_id];
    /* The core can't be set up twice in one process: starting again is a
     * fresh power-on of the same machine. */
    if (cur->running) {
        emulator_joystick(joystick_port, 0);
        emulator_reset(1);
        return 0;
    }
    snprintf(cur->sys_dir, sizeof(cur->sys_dir), "%s/system", dir);
    snprintf(cur->save_dir, sizeof(cur->save_dir), "%s/saves", dir);
    make_dir(cur->sys_dir);
    make_dir(cur->save_dir);
    cur->joy_port = joystick_port == 1 ? 1 : 2;
    cur->nvars = 0;

    cur->api->set_environment(environment);
    cur->api->set_video_refresh(video_refresh);
    cur->api->set_audio_sample(audio_sample);
    cur->api->set_audio_sample_batch(audio_batch);
    cur->api->set_input_poll(input_poll);
    cur->api->set_input_state(input_state);
    printf("emulator: starting the %s core\n", machine_names[cur_id]);
    cur->api->init();
    printf("emulator: booting\n");
    if (!cur->api->load_game(NULL)) {
        cur->api->deinit();
        return -1;
    }
    cur->api->set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    cur->api->get_system_av_info(&cur->av);
    /* The core finishes setting up in its first frame; loading before that
     * would set it up a second time (and crash). */
    printf("emulator: first frame\n");
    cur->api->run();
    cur->running = 1;
    attach_cart();
    printf("emulator: %ux%u (aspect %.3f), %.3f fps, audio %.0f Hz\n", cur->av.geometry.base_width,
           cur->av.geometry.base_height, cur->av.geometry.aspect_ratio, cur->av.timing.fps,
           cur->av.timing.sample_rate);
    return 0;
}

void emulator_stop(void)
{
    int shown = cur_id;
    for (int m = 0; m < EMU_MACHINES; m++) {     /* every machine started */
        if (!machines[m].running)
            continue;
        emulator_select(m);
        cur->api->unload_game();
        cur->api->deinit();
        cur->running = 0;
        cur->have_disk_cb = 0;
    }
    emulator_select(shown);
    have_frame = 0;
    audio_len = 0;
    ntaps = tap_step = 0;
}

int emulator_running(void) { return cur->running; }

void emulator_run_frame(void)
{
    if (!cur->running)
        return;
    step_taps();
    cur->api->run();
}

double emulator_aspect(void)    { return cur->running ? cur->av.geometry.aspect_ratio : 0; }
double emulator_fps(void)       { return cur->av.timing.fps > 0 ? cur->av.timing.fps : 50.125; }
int    emulator_sample_rate(void) { return cur->av.timing.sample_rate > 0 ? (int)cur->av.timing.sample_rate : 48000; }
int    emulator_fast(void)      { return cur->fast; }

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
    if (cur->cart_path[0] && cur->api->cartridge_attach_image(VICE_CARTRIDGE_CRT, cur->cart_path) != 0)
        fprintf(stderr, "emulator: can't attach the cartridge %s\n", cur->cart_path);
}

static int is_crt(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot && !strcasecmp(dot, ".crt");
}

int emulator_set_cartridge(const char *path)
{
    snprintf(cur->cart_path, sizeof(cur->cart_path), "%s", path ? path : "");
    if (!cur->running)
        return 0;
    emulator_release_keys();
    cur->api->cartridge_detach_image(-1);
    if (!cur->cart_path[0]) {
        cur->api->machine_trigger_reset(VICE_POWER_CYCLE);
        return 0;
    }
    if (cur->api->cartridge_attach_image(VICE_CARTRIDGE_CRT, cur->cart_path) != 0) {
        fprintf(stderr, "emulator: can't attach the cartridge %s\n", cur->cart_path);
        cur->cart_path[0] = 0;
        cur->api->machine_trigger_reset(VICE_POWER_CYCLE);
        return -1;
    }
    return 0;
}

int emulator_load(const char *path)
{
    if (!cur->running)
        return -1;
    emulator_release_keys();
    cur->api->unload_game();
    struct retro_game_info g = { path, NULL, 0, NULL };
    if (!cur->api->load_game(&g))
        return -1;
    /* A load into a cur->running core is otherwise only a plain reset; this
     * rebuilds the autostart command line and starts the file. */
    cur->api->reload_restart();
    if (!is_crt(path))
        attach_cart();                       /* (a cartridge game takes the slot) */
    return 0;
}

int emulator_insert_disk(const char *path)
{
    if (!cur->running)
        return -1;
    return cur->api->file_system_attach_disk(8, 0, path) == 0 ? 0 : -1;
}

void emulator_reset(int power_cycle)
{
    if (!cur->running)
        return;
    emulator_release_keys();
    if (power_cycle) {
        cur->api->unload_game();                 /* detach disks, tapes and cartridges */
        attach_cart();                       /* ...but the chosen cartridge stays in */
    }
    cur->api->machine_trigger_reset(power_cycle ? VICE_POWER_CYCLE : VICE_RESET_CPU);
}

int emulator_disk_count(void)
{
    return cur->running && cur->have_disk_cb && cur->disk_cb.get_num_images ? (int)cur->disk_cb.get_num_images() : 0;
}

int emulator_disk_index(void)
{
    return cur->running && cur->have_disk_cb && cur->disk_cb.get_image_index ? (int)cur->disk_cb.get_image_index() : 0;
}

int emulator_disk_insert(int index)
{
    if (index < 0 || index >= emulator_disk_count())
        return -1;
    cur->disk_cb.set_eject_state(true);
    bool ok = cur->disk_cb.set_image_index((unsigned)index);
    cur->disk_cb.set_eject_state(false);
    return ok ? 0 : -1;
}
