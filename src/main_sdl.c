/*
 * ps64 - display the C64 Ultimate VIC data stream in an SDL2 window and
 * play its audio stream. Builds for macOS and, with ps5-payload-sdk, as a
 * PS5 homebrew payload (__PROSPERO__).
 *
 *   ps64                          listen on UDP 11000 (video) and 11001 (audio)
 *   ps64 --start 192.168.1.64     ask the C64 to stream here via its REST API
 *   ps64 --multicast 239.0.1.64   join a multicast group instead of unicast
 *   ps64 --config ps64.cfg     read options from a file (key = value lines)
 *   ps64 --pick                   search the network and choose a C64
 *   ps64 --c64 emulator           use the built-in emulator (VICE) instead of a C64 Ultimate
 *
 * On PS5 the options are read from ps64.cfg next to the executable; with
 * no C64 configured the picker opens and the choice is saved there.
 * Once attached to a C64, a game controller drives it too (see controls.h).
 */
#include "a64.h"
#include "audiostream.h"
#include "c64api.h"
#include "c64keys.h"
#include "c64files.h"
#include "c64index.h"
#include "c64remote.h"
#include "controls.h"
#include "discover.h"
#include "favs.h"
#include "font_sdl.h"
#include "home_sdl.h"
#include "localfs.h"
#include "playlist.h"
#include "osk_sdl.h"
#include "settings.h"
#include "shots.h"
#include "ui.h"
#include "vicstream.h"
#ifdef PS64_EMU
#include "emugames.h"
#include "emulator.h"
#endif
#ifdef PS64_SID
#include "sidplay.h"
#endif

#include <SDL2/SDL.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    int         port;
    const char *start_host;   /* C64 IP to start/stop the stream via REST */
    const char *remote_host;  /* C64 IP to send controller input to (default: start_host) */
    const char *password;     /* optional Ultimate network password */
    const char *multicast;    /* multicast group to join for video */
    int         scale;
    int         swap_nibbles;
    const char *snapshot;     /* save one frame as BMP and exit */
    const char *screenshot;   /* debug: save the window, overlays included, and exit */
    int         osk_open;     /* debug: start with the on-screen keyboard open */
    int         osk_mods;     /* debug: ...with these C64MOD_* modifiers locked */
    int         audio;        /* play the audio stream */
    int         audio_port;
    const char *audio_multicast;
    int         audio_rate;
    int         audio_latency_ms;
    int         stream_volume;    /* dB, C64 Ultimate stream (its SIDs come in near full scale) */
    int         emulator_volume;  /* dB, built-in emulator (also loud next to other PS5 apps) */
    int         joystick_port;
    binding     bind[BTN_BINDABLE];
    int         bind_set[BTN_BINDABLE];
    const char *type_text;    /* debug: type this on the C64 at startup */
    int         listen_only;  /* c64 = none: don't attach to any C64 */
    int         pick;         /* open the C64 picker at launch */
    const char *browse_dir;   /* folder the file browser opens in */
    const char *browse_path;  /* debug: open the file browser here at launch */
    const char *ui_screen;    /* debug: open this screen at launch ("menu") */
    int         screenshot_ms;
    const char *debug_connect; /* debug: act as if this C64 was picked */
    const char *search_roots; /* folders the search index covers, comma-separated */
    int         build_index;  /* debug: build the search index, print stats, exit */
    const char *search_text;  /* debug: open the search screen with this query */
    const char *a64_url;      /* Assembly64 API base URL */
    const char *a64_search;   /* debug: search Assembly64 for this at launch */
    const char *a64_latest;   /* debug: show Assembly64's latest releases in this category */
    int         launch_fav;   /* debug: launch this favourite (1-based) at startup */
    int         use_emulator; /* c64 = emulator: the built-in emulator is the machine */
    const char *games_dir;    /* the emulator's games folder (default: "games" next to the config) */
    const char *emu_load;     /* debug: start this file in the emulator at launch */
    int         dpad_keys;    /* emulator: D-pad/stick/Cross as cursor keys and RETURN */
    int         no_home;      /* start straight away (debug flags imply it) instead of on the home screen */
    int         home_sel;     /* debug: the home screen's chosen object (-1: default) */
    const char *sid_file;     /* debug: play this .sid with the built-in SID player */
    const char *keys;         /* debug: buttons for the UI, one every 1.5 s from 2 s ("down,ok") */
    int         sid_song;     /* debug: its song (0: the start song) */
    const char *sids_dir;     /* the SID player's folder (default: "sids" next to the config) */
    const char *songlengths;  /* HVSC Songlengths.md5 */
    int         sid_volume;   /* dB, the SID player */
    int         sid_ultimate; /* the SID player plays on the C64 Ultimate (when there is one) */
} options;

/* A saved level onto the menu's steps: UI_VOLUME_MIN..0 dB in UI_VOLUME_STEP dB. */
static int clamp_volume(int db)
{
    if (db > 0)
        db = 0;
    if (db < UI_VOLUME_MIN)
        db = UI_VOLUME_MIN;
    return db / UI_VOLUME_STEP * UI_VOLUME_STEP;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [--port N] [--c64 C64_IP|none] [--remote C64_IP] [--password PW] [--pick]\n"
        "          [--multicast GROUP] [--scale N] [--swap-nibbles]\n"
        "          [--snapshot FILE.bmp] [--screenshot FILE.bmp] [--osk]\n"
        "          [--no-audio] [--audio-port N] [--audio-multicast GROUP]\n"
        "          [--audio-rate HZ] [--audio-latency MS] [--config FILE]\n"
        "          [--stream-volume DB] [--emulator-volume DB] [--no-home] [--home-sel N]\n"
        "          [--sid FILE] [--sid-song N] [--sids-dir DIR] [--songlengths FILE]\n"
        "          [--sid-volume DB] [--sid-output ps64|ultimate]\n"
        "          [--joystick-port 1|2] [--button-NAME KEY] [--type TEXT] [--browse-dir DIR]\n"
        "          [--search-roots /SD,/USB0] [--build-index] [--search TEXT]\n"
        "          [--assembly64-url URL] [--a64-search TEXT]\n"
        "          [--a64-latest CATEGORY]\n"
        "          [--c64 emulator] [--games-dir DIR] [--dpad joystick|keys] [--emu-load FILE]\n"
        "\n"
        "--c64 (alias --start) streams from and controls that C64; --remote only controls it;\n"
        "      --c64 emulator runs the built-in emulator instead (if this build has it).\n"
        "keys: F fullscreen, M mute, N swap nibble order, S save snapshot, Esc/Q quit,\n"
        "      O menu, B file browser, Tab on-screen keyboard\n"
        "controller: D-pad/stick joystick, Cross fire, touchpad keyboard, R3 Ultimate menu,\n"
        "      L3 file browser, Options menu\n"
        "--button-NAME: NAME is square, triangle, circle, l1, r1, l2 or r2; KEY is a C64\n"
        "      key (space, return, run_stop, f1..f8, home, clr, del, inst, a, 1, ...),\n"
        "      menu or none\n", argv0);
}

static int parse_args(int argc, char **argv, options *o)
{
    o->port = VIC_DEFAULT_PORT;
    o->start_host = NULL;
    o->remote_host = NULL;
    o->password = NULL;
    o->multicast = NULL;
    o->scale = 3;
    o->swap_nibbles = 0;
    o->snapshot = NULL;
    o->screenshot = NULL;
    o->osk_open = 0;
    o->osk_mods = 0;
    o->audio = 1;
    o->audio_port = AUDIO_DEFAULT_PORT;
    o->audio_multicast = NULL;
    o->audio_rate = AUDIO_RATE_PAL;
    o->audio_latency_ms = 60;
    o->stream_volume = -12;
    o->emulator_volume = -12;
    o->joystick_port = 2;
    memset(o->bind_set, 0, sizeof(o->bind_set));
    o->type_text = NULL;
    o->listen_only = 0;
    o->pick = 0;
    o->browse_dir = NULL;
    o->browse_path = NULL;
    o->ui_screen = NULL;
    o->screenshot_ms = 2000;
    o->debug_connect = NULL;
    o->search_roots = "/SD";
    o->build_index = 0;
    o->search_text = NULL;
    o->a64_url = A64_DEFAULT_URL;
    o->a64_search = NULL;
    o->a64_latest = NULL;
    o->launch_fav = 0;
    o->use_emulator = 0;
    o->games_dir = NULL;
    o->emu_load = NULL;
    o->dpad_keys = 0;
    o->no_home = 0;
    o->home_sel = -1;
    o->sid_file = NULL;
    o->keys = NULL;
    o->sid_song = 0;
    o->sids_dir = NULL;
#ifdef PS64_NATIVE
    o->songlengths = "/app0/assets/Songlengths.md5";
#else
    o->songlengths = "third_party/hvsc/Songlengths.md5";
#endif
    o->sid_volume = -12;
    o->sid_ultimate = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *next = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--port") && next)           { o->port = atoi(next); i++; }
        else if ((!strcmp(a, "--start") || !strcmp(a, "--c64")) && next) {
            o->listen_only = !strcasecmp(next, "none");
            o->use_emulator = !strcasecmp(next, "emulator");
            o->start_host = o->listen_only || o->use_emulator ? NULL : next;
            i++;
        }
        else if (!strcmp(a, "--games-dir") && next) { o->games_dir = next; i++; }
        else if (!strcmp(a, "--emu-load") && next)  { o->emu_load = next; i++; }
        else if (!strcmp(a, "--dpad") && next)      { o->dpad_keys = !strcasecmp(next, "keys"); i++; }
        else if (!strcmp(a, "--pick"))              { o->pick = 1; }
        else if (!strcmp(a, "--no-home"))           { o->no_home = 1; }
        else if (!strcmp(a, "--home-sel") && next)  { o->home_sel = atoi(next); i++; }
        else if (!strcmp(a, "--sid") && next)       { o->sid_file = next; i++; }
        else if (!strcmp(a, "--keys") && next)      { o->keys = next; i++; }
        else if (!strcmp(a, "--sid-song") && next)  { o->sid_song = atoi(next); i++; }
        else if (!strcmp(a, "--sids-dir") && next)  { o->sids_dir = next; i++; }
        else if (!strcmp(a, "--songlengths") && next) { o->songlengths = next; i++; }
        else if (!strcmp(a, "--sid-volume") && next) { o->sid_volume = atoi(next); i++; }
        else if (!strcmp(a, "--sid-output") && next) { o->sid_ultimate = !strcasecmp(next, "ultimate"); i++; }
        else if (!strcmp(a, "--browse-dir") && next) { o->browse_dir = next; i++; }
        else if (!strcmp(a, "--browse") && next)    { o->browse_path = next; i++; }
        else if (!strcmp(a, "--ui") && next)        { o->ui_screen = next; i++; }
        else if (!strcmp(a, "--screenshot-ms") && next) { o->screenshot_ms = atoi(next); i++; }
        else if (!strcmp(a, "--debug-connect") && next) { o->debug_connect = next; i++; }
        else if (!strcmp(a, "--search-roots") && next)  { o->search_roots = next; i++; }
        else if (!strcmp(a, "--build-index"))           { o->build_index = 1; }
        else if (!strcmp(a, "--search") && next)       { o->search_text = next; i++; }
        /* (Old configs set the Assembly64 client ID: PS64's own is built in now.) */
        else if (!strcmp(a, "--assembly64-client-id") && next) { i++; }
        else if (!strcmp(a, "--assembly64-url") && next)       { o->a64_url = next; i++; }
        else if (!strcmp(a, "--a64-search") && next)           { o->a64_search = next; i++; }
        else if (!strcmp(a, "--a64-latest") && next)           { o->a64_latest = next; i++; }
        else if (!strcmp(a, "--launch-favourite") && next)      { o->launch_fav = atoi(next); i++; }
        else if (!strcmp(a, "--remote") && next)    { o->remote_host = next; i++; }
        else if (!strcmp(a, "--password") && next)  { o->password = next; i++; }
        else if (!strcmp(a, "--multicast") && next) { o->multicast = next; i++; }
        else if (!strcmp(a, "--scale") && next)     { o->scale = atoi(next); i++; }
        else if (!strcmp(a, "--swap-nibbles"))      { o->swap_nibbles = 1; }
        else if (!strcmp(a, "--snapshot") && next)  { o->snapshot = next; i++; }
        else if (!strcmp(a, "--screenshot") && next) { o->screenshot = next; i++; }
        else if (!strcmp(a, "--osk"))               { o->osk_open = 1; }
        else if (!strcmp(a, "--osk-mods") && next)  { o->osk_open = 1; o->osk_mods = atoi(next); i++; }
        else if (!strcmp(a, "--no-audio"))          { o->audio = 0; }
        else if (!strcmp(a, "--audio-port") && next)      { o->audio_port = atoi(next); i++; }
        else if (!strcmp(a, "--audio-multicast") && next) { o->audio_multicast = next; i++; }
        else if (!strcmp(a, "--audio-rate") && next)      { o->audio_rate = atoi(next); i++; }
        else if (!strcmp(a, "--audio-latency") && next)   { o->audio_latency_ms = atoi(next); i++; }
        else if (!strcmp(a, "--stream-volume") && next)   { o->stream_volume = atoi(next); i++; }
        else if (!strcmp(a, "--emulator-volume") && next) { o->emulator_volume = atoi(next); i++; }
        else if (!strcmp(a, "--config") && next)          { i++; }   /* handled by load_config */
        else if (!strcmp(a, "--joystick-port") && next)   { o->joystick_port = atoi(next); i++; }
        else if (!strcmp(a, "--type") && next)            { o->type_text = next; i++; }
        else if (!strncmp(a, "--button-", 9) && next) {
            int b = controls_button_index(a + 9);
            if (b < 0 || controls_parse_binding(next, &o->bind[b]) != 0) {
                fprintf(stderr, "bad button binding: %s %s\n", a, next);
                return -1;
            }
            o->bind_set[b] = 1;
            i++;
        }
        else { usage(argv[0]); return -1; }
    }
    if (o->port <= 0 || o->port > 65535 || o->scale < 1 ||
        o->audio_port <= 0 || o->audio_port > 65535 || o->audio_port == o->port ||
        o->audio_rate < 8000 || o->audio_latency_ms < 10 ||
        (o->joystick_port != 1 && o->joystick_port != 2))
        return usage(argv[0]), -1;
    if (o->snapshot)
        o->audio = 0;
    o->stream_volume = clamp_volume(o->stream_volume);
    o->emulator_volume = clamp_volume(o->emulator_volume);
    o->sid_volume = clamp_volume(o->sid_volume);
    if (!o->remote_host)
        o->remote_host = o->start_host;
    /* Debug flags that go straight to a machine skip the home screen. */
    if (o->emu_load || o->sid_file || o->browse_path || o->a64_search || o->a64_latest || o->launch_fav ||
        o->snapshot || o->search_text || o->build_index || o->debug_connect || o->type_text ||
        (o->ui_screen && strcmp(o->ui_screen, "home")))
        o->no_home = 1;
    return 0;
}

/* ---- config file ---------------------------------------------------- */

/*
 * Turn "key = value" lines into "--key value" arguments placed before the
 * real command-line arguments, so the command line wins. "#" starts a comment.
 * Flag options take a value too: "no-audio = 1" (0/no/false to disable).
 */
static int load_config(const char *path, int argc, char **argv, int *out_argc, char ***out_argv)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    int cap = argc + 64, n = 0;
    char **args = calloc((size_t)cap, sizeof(*args));
    args[n++] = argv[0];
    char line[256];
    while (fgets(line, sizeof(line), f) && n < cap - argc - 2) {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line, *val = eq + 1, *end;
        while (*key == ' ' || *key == '\t') key++;
        for (end = key + strlen(key); end > key && (end[-1] == ' ' || end[-1] == '\t'); ) *--end = 0;
        while (*val == ' ' || *val == '\t') val++;
        for (end = val + strlen(val); end > val && strchr(" \t\r\n", end[-1]); ) *--end = 0;
        if (!*key || !*val) continue;

        char opt[64];
        snprintf(opt, sizeof(opt), "--%s", key);
        if (!strcmp(key, "no-audio") || !strcmp(key, "swap-nibbles")) {
            if (strcmp(val, "0") && strcasecmp(val, "no") && strcasecmp(val, "false"))
                args[n++] = strdup(opt);
        } else {
            args[n++] = strdup(opt);
            args[n++] = strdup(val);
        }
    }
    fclose(f);
    for (int i = 1; i < argc; i++)
        args[n++] = argv[i];
    *out_argc = n;
    *out_argv = args;
    return 0;
}

/* ---- notifications ------------------------------------------------------ */

#if defined(__PROSPERO__) && !defined(PS64_NATIVE)
int sceNotificationSend(int userId, bool isLogged, const char *payload);

/* Show a toast on the PS5; there is no window title or terminal to read. */
static void notify(const char *message)
{
    char json[1024];
    snprintf(json, sizeof(json),
        "{\"rawData\":{\"viewTemplateType\":\"InteractiveToastTemplateB\","
        "\"channelType\":\"Downloads\",\"useCaseId\":\"IDC\","
        "\"toastOverwriteType\":\"No\",\"isImmediate\":true,\"priority\":100,"
        "\"viewData\":{\"message\":{\"body\":\"PS64\"},"
        "\"subMessage\":{\"body\":\"%s\"}},"
        "\"platformViews\":{\"previewDisabled\":{\"viewData\":{\"message\":{\"body\":\"%s\"}}}}},"
        "\"createdDateTime\":\"2026-01-01T00:00:00.000Z\",\"localNotificationId\":\"6464\"}",
        message, message);
    sceNotificationSend(0xFE, true, json);
    printf("%s\n", message);
}
#else
static void notify(const char *message)
{
    printf("%s\n", message);
}
#endif

/* ---- networking ------------------------------------------------------- */

static int open_receiver(int port, const char *multicast)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("socket"); return -1; }

    int one = 1, rcvbuf = 4 * 1024 * 1024;
    /* No SO_REUSEPORT: a second viewer on the same port would silently take
     * the unicast stream, so let bind() fail loudly instead. */
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "bind UDP %d: %s\n", port, strerror(errno));
        close(s);
        return -1;
    }

    if (multicast) {
        struct ip_mreq mreq = {0};
        if (inet_pton(AF_INET, multicast, &mreq.imr_multiaddr) != 1) {
            fprintf(stderr, "bad multicast address: %s\n", multicast);
            close(s);
            return -1;
        }
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
            perror("IP_ADD_MEMBERSHIP");
            close(s);
            return -1;
        }
    }

    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    return s;
}

/* ---- audio ------------------------------------------------------------ */

/*
 * Packets are pushed into SDL's audio queue. Playback starts once
 * `target` frames are buffered; if the queue runs dry we pause and rebuffer.
 * If it grows past 2x target (a burst after a stall, or the sender's clock
 * running faster than ours) packets are dropped until it is back at target,
 * so latency doesn't stay high after a one-off burst.
 */
typedef struct {
    SDL_AudioDeviceID dev;
    Uint32   target;      /* frames */
    Uint32   prime;       /* extra frames before (re)starting: the device grabs a buffer's worth at once */
    int      buffering;
    int      draining;
    int      muted;
    int      gain;        /* Q15: 32768 is 0 dB */
    uint64_t underruns;
    uint64_t overflows;
} audio_out;

static int audio_open(audio_out *a, int rate, int latency_ms)
{
    memset(a, 0, sizeof(*a));
    SDL_AudioSpec want = {0}, have;
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);   /* SDL resamples to the device */
    if (!a->dev) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        return -1;
    }
    a->target = (Uint32)((Sint64)rate * latency_ms / 1000);
    a->prime = (Uint32)have.samples * 2;
    a->buffering = 1;
    a->gain = 32768;
    return 0;
}

static void audio_push(audio_out *a, const int16_t *samples, size_t frames)
{
    if (a->muted)
        return;
    Uint32 queued = SDL_GetQueuedAudioSize(a->dev) / 4;
    if (!a->buffering && queued == 0) {
        SDL_PauseAudioDevice(a->dev, 1);
        a->buffering = 1;
        a->underruns++;
    }
    if (queued > a->target * 2)
        a->draining = 1;
    else if (queued <= a->target)
        a->draining = 0;
    if (a->draining) {
        a->overflows++;
        return;
    }
    if (a->gain >= 32768) {
        SDL_QueueAudio(a->dev, samples, (Uint32)(frames * 4));
    } else {
        int16_t scaled[1024];
        size_t left = frames * 2;
        while (left) {
            size_t n = left < 1024 ? left : 1024;
            for (size_t i = 0; i < n; i++)
                scaled[i] = (int16_t)(((int32_t)samples[i] * a->gain) >> 15);
            SDL_QueueAudio(a->dev, scaled, (Uint32)(n * 2));
            samples += n;
            left -= n;
        }
    }
    if (a->buffering && queued + frames >= a->target + a->prime) {
        SDL_PauseAudioDevice(a->dev, 0);
        a->buffering = 0;
    }
}

/* Reopen at another rate (the emulator makes 48 kHz, the Ultimate ~47983 Hz). */
static void audio_reopen(audio_out *a, int rate, int latency_ms)
{
    printf("audio: reopening at %d Hz\n", rate);
    int muted = a->muted, gain = a->gain;
    SDL_CloseAudioDevice(a->dev);
    if (audio_open(a, rate, latency_ms) != 0)
        fprintf(stderr, "audio: can't reopen at %d Hz\n", rate);
    a->muted = muted;
    a->gain = gain;
}

static void audio_set_volume(audio_out *a, int db)
{
    a->gain = (int)(32768.0 * pow(10.0, db / 20.0) + 0.5);
}

static void audio_set_muted(audio_out *a, int muted)
{
    a->muted = muted;
    if (muted) {
        SDL_PauseAudioDevice(a->dev, 1);
        SDL_ClearQueuedAudio(a->dev);
        a->buffering = 1;
    }
}

/* ---- snapshots ------------------------------------------------------ */

static int save_snapshot(const vic_decoder *d, const char *path)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, d->width, d->height, 32,
                                                       SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return -1;
    vic_to_argb(d, surf->pixels, surf->pitch);
    int rc = SDL_SaveBMP(surf, path);
    SDL_FreeSurface(surf);
    printf("%s %s\n", rc == 0 ? "saved" : "failed to save", path);
    return rc;
}

/* Before the first frame: C64-style light blue border around a blue screen. */
static void draw_waiting(SDL_Renderer *ren)
{
    SDL_RenderSetLogicalSize(ren, VIC_MAX_WIDTH, 272);
    SDL_SetRenderDrawColor(ren, 0x70, 0x6D, 0xEB, 255);
    SDL_RenderClear(ren);
    SDL_Rect screen = { 32, 36, 320, 200 };
    SDL_SetRenderDrawColor(ren, 0x2E, 0x2C, 0x9B, 255);
    SDL_RenderFillRect(ren, &screen);
}

/* ---- typing text (debug) ------------------------------------------------ */

/*
 * Queue text; "\n" (backslash n) means RETURN, and "{name}" sends a named
 * key (any c64keys name: {f1}, {run_stop}, {home}, {cursor_up}...) or {menu}.
 */
static void type_text(remote *r, const char *text)
{
    for (const char *p = text; *p; p++) {
        int ch = *p, key, mods;
        const char *close = ch == '{' ? strchr(p, '}') : NULL;
        if (close) {
            char name[32];
            snprintf(name, sizeof(name), "%.*s", (int)(close - p - 1), p + 1);
            if (!strcmp(name, "menu"))
                remote_menu_button(r);
            else if (c64key_parse(name, &key, &mods) == 0)
                remote_key_tap(r, key, mods);
            else
                fprintf(stderr, "type: unknown key {%s}\n", name);
            p = close;
            continue;
        }
        if (ch == '\\' && p[1] == 'n') {
            ch = '\n';
            p++;
        }
        if (c64key_from_ascii(ch, &key, &mods) == 0)
            remote_key_tap(r, key, mods);
        else
            fprintf(stderr, "type: no C64 key for '%c'\n", ch);
    }
}

/* ---- C64 session ---------------------------------------------------------- */

/*
 * The C64 we're attached to: its REST address, whether we started its
 * streams (so we stop them again), and the workers that send it input
 * and browse its files. Replaced as a whole when another C64 is chosen.
 */
typedef struct {
    char      host[64];
    char      password[64];
    c64api    api;
    int       streaming;
    remote   *rem;
    c64files *files;
    c64index *index;          /* search index for this C64 (may be empty) */
    int       emu;            /* the built-in emulator instead of a C64 Ultimate */
} session;

/* Where the search index lives: next to the config file, else the current folder. */
static char index_file[600] = "ps64-index.txt";
static char favs_file[600] = "ps64-favourites.txt";
static char recents_file[600] = "ps64-recents.txt";

/* Files kept next to the config (or in the current folder without one). */
static void next_to_config(const char *config, const char *name, char *out, size_t cap)
{
    if (!config)
        return;
    const char *slash = strrchr(config, '/');
    snprintf(out, cap, "%.*s%s%s", slash ? (int)(slash - config) : 0, config, slash ? "/" : "", name);
}

static void set_index_file(const char *config)
{
    next_to_config(config, "ps64-index.txt", index_file, sizeof(index_file));
    next_to_config(config, "ps64-favourites.txt", favs_file, sizeof(favs_file));
    next_to_config(config, "ps64-recents.txt", recents_file, sizeof(recents_file));
}

/* Remember a launch in Recents (not mount-only actions). */
static void record_recent(fav_list *recents, fav_entry e, int action)
{
    if (action == FA_MOUNT_A || action == FA_MOUNT_B)
        return;
    e.action = action;
    fav_add_recent(recents, &e);
    fav_save(recents, recents_file);
}

static void session_end(session *s, const options *o)
{
    if (s->emu)
        files_set_emulator(0);    /* the emulator itself keeps its state, unused, until next time */
    index_free(s->index);
    files_stop(s->files);
    remote_stop(s->rem);
    if (s->streaming) {
        c64api_stop_stream(&s->api, "video");
        if (o->audio)
            c64api_stop_stream(&s->api, "audio");
    }
    memset(s, 0, sizeof(*s));
}

/*
 * Attach to host: start its streams towards us if stream is set, then the
 * input and file workers. Returns 0, -1 if unreachable, or the HTTP status
 * of a refused stream request (403: wrong password).
 */
static int session_begin(session *s, const options *o, const char *host, const char *password,
                         int stream)
{
    snprintf(s->host, sizeof(s->host), "%s", host);
    snprintf(s->password, sizeof(s->password), "%s", password ? password : "");
    s->api.host = s->host;
    s->api.password = s->password[0] ? s->password : NULL;
    int rc = 0;
    if (stream) {
        char me[INET_ADDRSTRLEN], path[128];
        if (c64api_local_ip(s->host, me, sizeof(me)) != 0)
            return -1;
        snprintf(path, sizeof(path), "/v1/streams/video:start?ip=%s:%d", me, o->port);
        int status = c64api_request(&s->api, "PUT", path, NULL, NULL, 0, NULL, 0, NULL);
        printf("start video stream on %s -> %s: HTTP %d\n", s->host, me, status);
        if (status < 200 || status >= 300)
            return status < 0 ? -1 : status;
        s->streaming = 1;
        if (o->audio)
            c64api_start_stream(&s->api, "audio", o->audio_port, AUDIO_DEFAULT_PORT);
    }
    s->rem = remote_start(s->host, s->api.password);
    s->files = files_start(s->host, s->api.password, s->rem);
    s->index = index_new();
    if (s->index && index_load(s->index, index_file, s->host) == 0)
        printf("search index: %d files from %s\n", index_count(s->index), index_file);
    return rc;
}

#ifdef PS64_EMU
/*
 * The built-in emulator as the machine: its games folder, its own files
 * (VICE's settings and saves, Assembly64 downloads) and search index.
 */
static char games_dir[600] = "games", emu_dir[600] = "emulator";
static char emu_index_file[600] = "ps64-emu-index.txt";

static void emu_tap(int key, int mods)       { emulator_key_tap(key, mods); }
static void emu_joy(int port, unsigned bits) { emulator_joystick(port, bits); }
static void emu_release(void)                { emulator_release_keys(); }

static int session_begin_emulator(session *s, const options *o)
{
    /* Started before (then left for the home screen): carry on where it was. */
    if (emulator_running())
        emulator_joystick(o->joystick_port, 0);
    else if (emulator_start(emu_dir, o->joystick_port) != 0)
        return -1;
    static const remote_sink sink = { emu_tap, emu_joy, emu_release };
    s->emu = 1;
    s->rem = remote_start_local(&sink);
    files_set_emulator(1);
    localfs_mkdirs(games_dir);
    s->index = index_new();
    if (s->index && index_load(s->index, emu_index_file, INDEX_LOCAL_HOST) == 0)
        printf("search index: %d files from %s\n", index_count(s->index), emu_index_file);
    return 0;
}

/* List a games-folder path into the browser. */
static void emu_list(ui_state *ui, const char *path)
{
    enum { MAX = 4096 };
    ftp_entry *e = malloc(sizeof(*e) * MAX);
    char err[160] = "Out of memory";
    int n = e ? localfs_list(games_dir, path, e, MAX, err, sizeof(err)) : -1;
    if (n < 0) {
        free(e);
        ui_set_list_error(ui, path, err);
    } else {
        ui_set_listing(ui, path, e, n);
    }
}

static double now_ms(void)
{
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

/*
 * Run the emulator for this pass of the main loop: 50 frames a second,
 * nudged by the sound queue so audio neither runs dry nor piles up; while a
 * disk loads, as many frames as fit in 12 ms without sound. Returns the
 * frames run (0: no new picture).
 */
#ifdef PS64_SID
/* ---- SID player -------------------------------------------------------- */

#define SID_FA 1000                   /* a64_download's action for the SID player's tunes */
#define SID_MAX_BYTES (128 * 1024)    /* .sid files are at most ~64 KB */
static char sids_dir[600] = "sids", sid_cache_dir[600] = "sid-cache";

/* The tune playing and the list it plays from (copied from the UI's list). */
typedef struct {
    sidplay       *engine;            /* also when it plays on the C64 Ultimate (header info) */
    playlist       list;
    int            tab, n;            /* SID_TAB_* of the list, its tunes */
    int           *row;               /* tune -> entry in the UI's list (to mark it playing) */
    char         (*paths)[512];       /* SID_TAB_LOCAL: SID-folder paths */
    a64_item      *items;             /* SID_TAB_A64 */
    unsigned char *data;              /* the tune's bytes (sent again for another subtune) */
    size_t         len;
    char           name[160];
    int            paused;
    int            prefer_ultimate;   /* "Play on: C64 Ultimate" chosen (sid-output) */
    int            on_ultimate;       /* this tune plays there (one is connected) */
    char           want_id[64];       /* the Assembly64 tune being fetched */
    int            want_cat;
    Uint32         ult_started;       /* on the C64 Ultimate: when its song started */
    int16_t        scope[SID_SCOPE];
} sid_player;
static sid_player sp;
static Uint32 sid_render_ms;          /* time spent rendering, for the log */

/* Append stereo sound to the scope's window (mono, the latest SID_SCOPE samples). */
static void sid_scope_add(const int16_t *stereo, int frames)
{
    int from = frames > SID_SCOPE ? frames - SID_SCOPE : 0, add = frames - from;
    memmove(sp.scope, sp.scope + add, sizeof(sp.scope[0]) * (size_t)(SID_SCOPE - add));
    for (int k = from; k < frames; k++)
        sp.scope[SID_SCOPE - (frames - k)] = (int16_t)((stereo[k * 2] + stereo[k * 2 + 1]) / 2);
}

/* Keep the audio queue topped up with freshly rendered sound (it renders ahead by the
 * queue's length, so sound and the picture stay in step); the end of it is the scope. */
static void sid_step(audio_out *a, sidplay *p)
{
    Uint32 queued = SDL_GetQueuedAudioSize(a->dev) / 4;
    Uint32 want = a->target + a->prime + 256;
    static int16_t buf[4096 * 2];
    while (queued < want) {
        int n = (int)(want - queued) > 4096 ? 4096 : (int)(want - queued);
        Uint32 t0 = SDL_GetTicks();
        int got = sidplay_render(p, buf, n);
        sid_render_ms += SDL_GetTicks() - t0;
        if (got <= 0)
            break;
        audio_push(a, buf, (size_t)got);
        queued += (Uint32)got;
        sid_scope_add(buf, got);
        if (a->draining)
            break;
    }
}

/* Without sound (muted, or --no-audio): still play the tune in time, discarding it. */
static void sid_step_silent(sidplay *p, Uint32 now)
{
    static Uint32 last;
    static int16_t buf[4096 * 2];
    int frames = last && now - last < 500 ? (int)((now - last) * (SIDPLAY_RATE / 1000)) : 0;
    last = now;
    while (frames > 0) {
        int n = frames > 4096 ? 4096 : frames;
        int got = sidplay_render(p, buf, n);
        if (got <= 0)
            break;
        frames -= got;
        sid_scope_add(buf, got);
    }
}

/* The tune's file in the Assembly64 cache. */
static void sid_cache_path(const a64_item *it, char *out, size_t cap)
{
    char id[64];
    snprintf(id, sizeof(id), "%s", it->id);
    for (char *c = id; *c; c++)
        if (!isalnum((unsigned char)*c))
            *c = '_';
    snprintf(out, cap, "%s/%s-%d.sid", sid_cache_dir, id, it->category);
}

static unsigned char *read_file(const char *path, size_t max, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    unsigned char *data = malloc(max);
    *len = data ? fread(data, 1, max, f) : 0;
    fclose(f);
    if (!*len) {
        free(data);
        return NULL;
    }
    return data;
}

/* Copy the playing tune's details into the UI. */
static void sid_show(ui_state *ui)
{
    ui->sid_active = sp.engine != NULL;
    ui->sid_paused = sp.paused;
    ui->sid_on_ultimate = sp.on_ultimate;
    int cur = playlist_current(&sp.list);
    ui->sid_play_tab = sp.n ? sp.tab : -1;
    ui->sid_play_index = sp.n && cur >= 0 ? sp.row[cur] : -1;
    if (!sp.engine)
        return;
    sidplay_info si;
    sidplay_get_info(sp.engine, &si);
    snprintf(ui->sid_title, sizeof(ui->sid_title), "%s", si.title[0] ? si.title : sp.name);
    snprintf(ui->sid_author, sizeof(ui->sid_author), "%s", si.author);
    snprintf(ui->sid_released, sizeof(ui->sid_released), "%s", si.released);
    ui->sid_song = si.song;
    ui->sid_songs = si.songs;
    ui->sid_model = si.model;
    ui->sid_ntsc = si.ntsc;
    ui->sid_length_ms = si.length_ms;
}

/* Stop: the engine, and the C64 Ultimate if it was playing there. */
static void sid_stop(ui_state *ui, c64files *files)
{
    if (sp.engine)
        printf("sid: stop\n");
    if (sp.on_ultimate && sp.engine && files)
        files_action(files, "", FA_RESET);
    sidplay_close(sp.engine);
    sp.engine = NULL;
    free(sp.data);
    sp.data = NULL;
    sp.want_id[0] = 0;
    sp.paused = 0;
    ui->sid_active = 0;
    ui->sid_status[0] = 0;
    memset(sp.scope, 0, sizeof(sp.scope));
}

/* Play a tune's bytes (takes data): on PS64, or sent to the C64 Ultimate. */
static int sid_play_bytes(ui_state *ui, c64files *files, const char *name, unsigned char *data,
                          size_t len)
{
    sidplay_close(sp.engine);
    free(sp.data);
    sp.data = NULL;
    char err[128];
    sp.engine = sidplay_open(data, len, err, sizeof(err));
    if (!sp.engine) {
        snprintf(ui->sid_status, sizeof(ui->sid_status), "Can't play %s: %s", name, err);
        free(data);
        return -1;
    }
    sp.data = data;
    sp.len = len;
    snprintf(sp.name, sizeof(sp.name), "%s", name);
    printf("sid: playing %s (%zu bytes)%s\n", name, len,
           sp.prefer_ultimate && ui->sid_can_ultimate && files ? " on the C64 Ultimate" : "");
    ui->sid_status[0] = 0;
    sp.paused = 0;
    sp.on_ultimate = sp.prefer_ultimate && ui->sid_can_ultimate && files;
    if (sp.on_ultimate) {
        unsigned char *copy = malloc(len);
        if (copy) {
            memcpy(copy, data, len);
            files_play_sid(files, name, copy, len, 0);
        }
        sp.ult_started = SDL_GetTicks();
    }
    sid_show(ui);
    return 0;
}

/* Start the playlist's current tune: a file, the Assembly64 cache, or a download. */
static void sid_load_current(ui_state *ui, a64 *a64c, c64files *files)
{
    int cur = playlist_current(&sp.list);
    if (cur < 0)
        return;
    sp.want_id[0] = 0;
    if (sp.tab == SID_TAB_LOCAL) {
        char path[1200];
        size_t len;
        localfs_real_path(sids_dir, sp.paths[cur], path, sizeof(path));
        unsigned char *data = read_file(path, SID_MAX_BYTES, &len);
        const char *name = strrchr(sp.paths[cur], '/');
        if (data)
            sid_play_bytes(ui, files, name ? name + 1 : sp.paths[cur], data, len);
        else
            snprintf(ui->sid_status, sizeof(ui->sid_status), "Can't read %s", sp.paths[cur]);
    } else {
        const a64_item *it = &sp.items[cur];
        char path[700];
        size_t len;
        sid_cache_path(it, path, sizeof(path));
        unsigned char *data = read_file(path, SID_MAX_BYTES, &len);
        if (data) {
            sid_play_bytes(ui, files, it->name, data, len);
        } else if (a64c) {
            sidplay_close(sp.engine);                /* silence while it downloads */
            sp.engine = NULL;
            snprintf(sp.want_id, sizeof(sp.want_id), "%s", it->id);
            sp.want_cat = it->category;
            a64_entries(a64c, it->id, it->category);
            snprintf(ui->sid_status, sizeof(ui->sid_status), "Fetching %s...", it->name);
        }
    }
    sid_show(ui);
}

/* Play from the UI's list: entry index of tab (folders are skipped). */
static void sid_play_list(ui_state *ui, int tab, int index, a64 *a64c, c64files *files, Uint32 now)
{
    free(sp.row);
    free(sp.paths);
    free(sp.items);
    sp.row = NULL;
    sp.paths = NULL;
    sp.items = NULL;
    sp.n = 0;
    sp.tab = tab;
    int count = tab == SID_TAB_LOCAL ? ui->sid_count : ui->sid_a64_count, start = 0;
    sp.row = malloc(sizeof(int) * (size_t)(count ? count : 1));
    if (tab == SID_TAB_LOCAL) {
        sp.paths = malloc(sizeof(*sp.paths) * (size_t)(count ? count : 1));
        for (int i = 0; sp.row && sp.paths && i < count; i++) {
            if (ui->sid_entries[i].is_dir)
                continue;
            if (i == index)
                start = sp.n;
            snprintf(sp.paths[sp.n], sizeof(sp.paths[sp.n]), "%s%s%s", ui->sid_path,
                     strcmp(ui->sid_path, "/") ? "/" : "", ui->sid_entries[i].name);
            sp.row[sp.n++] = i;
        }
    } else {
        sp.items = malloc(sizeof(*sp.items) * (size_t)(count ? count : 1));
        for (int i = 0; sp.row && sp.items && i < count; i++) {
            sp.items[sp.n] = ui->sid_a64[i];
            sp.row[sp.n++] = i;
        }
        start = index;
    }
    playlist_start(&sp.list, sp.n, start, ui->sid_mode, now);
    sid_load_current(ui, a64c, files);
}

/* List a folder of the SID folder into the player. */
static void sid_list(ui_state *ui, const char *path)
{
    enum { MAX = 4096 };
    ftp_entry *e = malloc(sizeof(*e) * MAX);
    char err[160] = "Out of memory";
    localfs_mkdirs(sids_dir);
    int n = e ? localfs_list(sids_dir, path, e, MAX, err, sizeof(err)) : -1;
    if (n < 0) {
        free(e);
        ui_sid_list_error(ui, path, err);
    } else {
        ui_sid_set_listing(ui, path, e, n);
    }
}
#endif

static int emu_step(audio_out *a, int audio_on)
{
    static double next;
    static int16_t buf[8192 * 2];
    double now = now_ms();
    int ran = 0;
    if (emulator_fast()) {
        do {
            emulator_run_frame();
            ran++;
            while (emulator_take_audio(buf, 8192))
                ;
        } while (emulator_fast() && now_ms() < now + 12);
        next = now_ms();
        return ran;
    }
    if (next == 0 || now - next > 200)
        next = now;                       /* first frame, or after a stall */
    double adjust = 1.0;
    if (audio_on && !a->muted && !a->buffering && a->target) {
        double err = ((double)SDL_GetQueuedAudioSize(a->dev) / 4 - a->target) / a->target;
        adjust += 0.01 * (err > 1 ? 1 : err < -1 ? -1 : err);
    }
    double frame_ms = 1000.0 / emulator_fps() * adjust;
    while (now >= next && ran < 3) {
        emulator_run_frame();
        ran++;
        next += frame_ms;
        size_t n;
        while ((n = emulator_take_audio(buf, 8192)) > 0)
            if (audio_on)
                audio_push(a, buf, n);
    }
    return ran;
}
#endif

/* Explain a session_begin failure. */
static void connect_error(char *msg, size_t cap, int rc, const char *host)
{
    if (rc == 403)
        snprintf(msg, cap, "Wrong network password for the C64 at %s", host);
    else if (rc < 0)
        snprintf(msg, cap, "Can't reach the C64 at %s - is its web remote control on?", host);
    else
        snprintf(msg, cap, "The C64 at %s refused to start its streams (HTTP %d)", host, rc);
}

/* ---- main ------------------------------------------------------------- */

#ifdef PS64_NATIVE
/*
 * The native app (PPSA06464) keeps its files in /data/ps64, because a store
 * update replaces the app's own folder. The first time, it brings over the
 * settings, lists and indexes of the payload version in /data/homebrew/PS64.
 *
 * The dev build (PS64_DEV: PPSA06465, "PS64 Dev", see docs/RELEASING.md) runs
 * next to the store copy with its own folder, /data/ps64-dev, and starts from
 * a copy of the store copy's files (or else the payload version's).
 */
#ifdef PS64_DEV
#define NATIVE_DIR "/data/ps64-dev"
static const char *const first_run_sources[] = { "/data/ps64", "/data/homebrew/PS64" };
#else
#define NATIVE_DIR "/data/ps64"
static const char *const first_run_sources[] = { "/data/homebrew/PS64" };
#endif

static void copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in)
        return;
    FILE *out = fopen(to, "wb");
    if (out) {
        char buf[16384];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
            fwrite(buf, 1, n, out);
        fclose(out);
        printf("copied %s -> %s\n", from, to);
    }
    fclose(in);
}

/* (access() doesn't report existing files reliably in a native app: it said
 * "missing" for a file it had just written, so the copied config was overwritten.) */
static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

static void native_first_run(void)
{
    mkdir(NATIVE_DIR, 0777);
    static const char *const files[][2] = {
        { "ps64.cfg", "ps64.cfg" }, { "c64view.cfg", "ps64.cfg" },
        { "ps64-favourites.txt", "ps64-favourites.txt" }, { "ps64-recents.txt", "ps64-recents.txt" },
        { "ps64-index.txt", "ps64-index.txt" }, { "ps64-emu-index.txt", "ps64-emu-index.txt" },
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char to[256];
        snprintf(to, sizeof(to), NATIVE_DIR "/%s", files[i][1]);
        /* Each file from the first place that has it. */
        for (size_t s = 0; s < sizeof(first_run_sources) / sizeof(first_run_sources[0]); s++) {
            char from[256];
            snprintf(from, sizeof(from), "%s/%s", first_run_sources[s], files[i][0]);
            if (!file_exists(to) && file_exists(from))
                copy_file(from, to);
        }
    }
    if (!file_exists(NATIVE_DIR "/ps64.cfg")) {
        FILE *f = fopen(NATIVE_DIR "/ps64.cfg", "w");
        if (f) {
            fputs("# PS64 settings: one \"key = value\" per line (see INSTALL.txt).\n", f);
            fclose(f);
        }
    }
}

int sceSystemServiceLoadExec(const char *path, char *const argv[]);
static int ps64_main(int argc, char **argv);

/* The app's entry point: it can't return from main, the system closes it. */
#include <ucontext.h>

/*
 * A crash: put where it happened in the log (the console only reports crashes in an
 * app's first minute). The program counter less this handler's address, plus the
 * handler's address in the build (nm eboot.elf), gives the function.
 */
static void crash_handler(int sig, siginfo_t *info, void *context)
{
    char line[200];
    unsigned long long pc = 0;
#if defined(__x86_64__)
    pc = (unsigned long long)((ucontext_t *)context)->uc_mcontext.mc_rip;
#endif
    unsigned long long here = (unsigned long long)(uintptr_t)crash_handler;
    int n = snprintf(line, sizeof(line),
                     "CRASH: signal %d at address %p, pc 0x%llx (crash_handler%+lld)\n",
                     sig, info ? info->si_addr : NULL, pc, (long long)(pc - here));
    if (n > 0)
        write(STDOUT_FILENO, line, (size_t)n);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void crash_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
        sigaction(sigs[i], &sa, NULL);
}

int main(int argc, char **argv)
{
    static char *fallback[] = { "/app0/eboot.bin", NULL };
    if (argc < 1 || !argv || !argv[0]) {
        argc = 1;
        argv = fallback;
    }
    native_first_run();
    /* No terminal: keep what PS64 prints in a log next to its settings. */
    if (freopen(NATIVE_DIR "/ps64.log", "w", stdout)) {
        setvbuf(stdout, NULL, _IOLBF, 0);
        dup2(fileno(stdout), fileno(stderr));
        setvbuf(stderr, NULL, _IONBF, 0);
    }
    crash_handlers();
    printf("PS64 native app starting\n");
    ps64_main(argc, argv);
    printf("PS64 closing\n");
    fflush(stdout);
    sceSystemServiceLoadExec("exit", NULL);
    for (;;)
        sleep(1);
}
#define main ps64_main
#elif defined(__PROSPERO__)
/* SDL2main provides main() on PS5 and calls SDL_main. */
#define main SDL_main
#endif

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);   /* REST calls to a C64 that hangs up mid-request */

    /* Config file: --config FILE, or ps64.cfg next to the executable on PS5. */
    const char *config = NULL;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--config"))
            config = argv[i + 1];
#ifdef PS64_NATIVE
    if (!config)
        config = NATIVE_DIR "/ps64.cfg";
#elif defined(__PROSPERO__)
    char default_config[512];
    if (!config) {
        const char *slash = strrchr(argv[0], '/');
        int dirlen = slash ? (int)(slash - argv[0]) : 0;
        snprintf(default_config, sizeof(default_config), "%.*s/ps64.cfg",
                 dirlen, slash ? argv[0] : "/data/homebrew/PS64");
        /* Before the rename to PS64 the config was c64view.cfg: keep using it. */
        FILE *probe = fopen(default_config, "r");
        if (probe) {
            fclose(probe);
        } else {
            char legacy[512];
            snprintf(legacy, sizeof(legacy), "%.*s/c64view.cfg",
                     dirlen, slash ? argv[0] : "/data/homebrew/PS64");
            if ((probe = fopen(legacy, "r")) != NULL) {
                fclose(probe);
                snprintf(default_config, sizeof(default_config), "%s", legacy);
            }
        }
        config = default_config;
    }
#endif
    int cargc = argc;
    char **cargv = argv;
    if (config) {
        if (load_config(config, argc, argv, &cargc, &cargv) == 0)
            printf("config: %s\n", config);
        else
            fprintf(stderr, "config: can't read %s, using defaults\n", config);
    }

    options opt;
    if (parse_args(cargc, cargv, &opt) != 0) {
        notify("Bad options in ps64.cfg");
        return 2;
    }
    set_index_file(config);
#ifdef PS64_EMU
    next_to_config(config, "games", games_dir, sizeof(games_dir));
    next_to_config(config, "emulator", emu_dir, sizeof(emu_dir));
    next_to_config(config, "ps64-emu-index.txt", emu_index_file, sizeof(emu_index_file));
    if (opt.games_dir)
        snprintf(games_dir, sizeof(games_dir), "%s", opt.games_dir);
    static emugames eg;
    eg_init(&eg, games_dir, emu_dir);
#endif

    int sock = open_receiver(opt.port, opt.multicast);
    if (sock < 0) {
        notify("Can't open the video port (is another viewer running?)");
        return 1;
    }
    printf("video: listening on UDP %d%s%s\n", opt.port,
           opt.multicast ? ", multicast " : "", opt.multicast ? opt.multicast : "");

    int asock = -1;
    if (opt.audio) {
        asock = open_receiver(opt.audio_port, opt.audio_multicast);
        if (asock < 0) {
            notify("Can't open the audio port (is another viewer running?)");
            return 1;
        }
        printf("audio: listening on UDP %d%s%s\n", opt.audio_port,
               opt.audio_multicast ? ", multicast " : "",
               opt.audio_multicast ? opt.audio_multicast : "");
    }

    static vic_decoder dec;
    vic_init(&dec);
    dec.swap_nibbles = opt.swap_nibbles;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | (opt.audio ? SDL_INIT_AUDIO : 0)) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    static audio_decoder adec;
    static int16_t samples[AUDIO_MAX_FRAMES * 2];
    audio_out aout;
    int aout_rate = opt.audio_rate;     /* the device's rate: the Ultimate's, or the emulator's */
    audio_init(&adec);
    if (opt.audio && audio_open(&aout, opt.audio_rate, opt.audio_latency_ms) != 0) {
        fprintf(stderr, "warning: continuing without audio\n");
        opt.audio = 0;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");   /* crisp pixels */
#ifdef __PROSPERO__
    SDL_Window *win = SDL_CreateWindow("PS64", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        1920, 1080, SDL_WINDOW_FULLSCREEN);
#else
    SDL_Window *win = SDL_CreateWindow("C64 Ultimate VIC stream",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        VIC_MAX_WIDTH * opt.scale, 272 * opt.scale,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
#endif
    /* Don't insist on acceleration: the PS5 SDL port only has the software
     * renderer unless it's built with Mesa. */
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (win && !ren)
        ren = SDL_CreateRenderer(win, -1, 0);
    if (!win || !ren) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        notify("Can't open the display");
        return 1;
    }
    SDL_RendererInfo rinfo;
    SDL_GetRendererInfo(ren, &rinfo);
    int has_vsync = (rinfo.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    printf("renderer: %s%s\n", rinfo.name, has_vsync ? ", vsync" : "");

    SDL_Texture *tex = NULL;
    int tex_w = 0, tex_h = 0;
    uint8_t buf[2048];
    int running = 1, fullscreen = 0, dirty = 0;
    Uint32 started = SDL_GetTicks(), stats_at = started;
    uint64_t frames_at = 0, emu_frames = 0;
    int was_emu = 0;

    /* The first frames after the window opens can stall for ~0.5s, which
     * would arrive as an audio burst; discard audio until things settle. */
    Uint32 audio_from = SDL_GetTicks() + 1000;

    static ui_state ui;
    ui_setup(&ui);

    /* Favourites and recents. */
    static fav_list favs, recents;
    fav_init(&favs, 0);
    fav_init(&recents, RECENTS_MAX);
    fav_load(&favs, favs_file);
    fav_load(&recents, recents_file);
    ui.favs = &favs;
    ui.recents = &recents;
    ui.port = opt.joystick_port;
    controls ctl;
    controls_init(&ctl, NULL, &ui, opt.joystick_port);
    for (int b = 0; b < BTN_BINDABLE; b++)
        if (opt.bind_set[b])
            ctl.bind[b] = opt.bind[b];
    ctl.osk.open = opt.osk_open;
    ctl.osk.shift = opt.osk_mods & C64MOD_SHIFT ? 2 : 0;
    ctl.osk.cbm = (opt.osk_mods & C64MOD_CBM) != 0;
    ctl.osk.ctrl = (opt.osk_mods & C64MOD_CTRL) != 0;
    ctl.joy_keys = opt.dpad_keys;
    char browse_dir[512];
    snprintf(browse_dir, sizeof(browse_dir), "%s", opt.browse_dir ? opt.browse_dir : "/");

    int sid_playing = 0;              /* the SID player has the sound (on PS64) */
#ifdef PS64_SID
    next_to_config(config, "sids", sids_dir, sizeof(sids_dir));
    next_to_config(config, "sid-cache", sid_cache_dir, sizeof(sid_cache_dir));
    if (opt.sids_dir)
        snprintf(sids_dir, sizeof(sids_dir), "%s", opt.sids_dir);
    sp.prefer_ultimate = opt.sid_ultimate;
    /* Debug: play a .sid straight away. */
    if (opt.sid_file) {
        size_t len;
        unsigned char *data = read_file(opt.sid_file, SID_MAX_BYTES, &len);
        if (sidplay_songlengths(opt.songlengths) != 0)
            printf("sid: no song lengths (%s)\n", opt.songlengths);
        if (!data || sid_play_bytes(&ui, NULL, opt.sid_file, data, len) != 0)
            printf("sid: %s: %s\n", opt.sid_file, data ? ui.sid_status : "can't read it");
        if (sp.engine && opt.sid_song)
            sidplay_select(sp.engine, opt.sid_song);
        if (sp.engine) {
            sid_show(&ui);
            printf("sid: %s / %s / %s  song %d of %d  %s  %d ms\n", ui.sid_title, ui.sid_author,
                   ui.sid_released, ui.sid_song, ui.sid_songs, ui.sid_model == SIDPLAY_MODEL_8580 ? "8580" : "6581",
                   ui.sid_length_ms);
        }
    }
#endif

    /* Attach to the configured C64, or let the user pick one. */
    static session ses;
    discover *scan = NULL;
    int want_picker = opt.pick;
    char msg[160];
    /* The home screen: the C64 Ultimate (the saved one, or the only one on the network) is
     * looked for in the background and its stream started, so the 1084 shows it live. */
    char home_want[16] = "", home_pass[64] = "";
    Uint32 home_search_at = 0, stream_since = 0, last_frame_tick = 0;
    uint64_t frames_seen = 0;
    int home_locked = 0;
    if (opt.start_host)
        snprintf(home_want, sizeof(home_want), "%s", opt.start_host);
    if (opt.password)
        snprintf(home_pass, sizeof(home_pass), "%s", opt.password);
    if (!opt.no_home) {
        ui_open_home(&ui);
        if (opt.home_sel >= 0 && opt.home_sel < HOME_ITEMS)
            ui.home_sel = opt.home_sel;
        snprintf(msg, sizeof(msg), "PS64");
        if (want_picker) {
            ui_open_picker(&ui, NULL);
            ui.picker_from_home = 1;
            scan = discover_start(home_want[0] ? home_want : NULL, home_pass);
        }
        want_picker = 0;
    } else if (opt.use_emulator) {
#ifdef PS64_EMU
        if (session_begin_emulator(&ses, &opt) == 0) {
            snprintf(msg, sizeof(msg), "Built-in emulator");
        } else {
            session_end(&ses, &opt);
            snprintf(msg, sizeof(msg), "The built-in emulator didn't start");
            want_picker = 1;
        }
#else
        snprintf(msg, sizeof(msg), "This PS64 build has no built-in emulator");
        want_picker = 1;
#endif
    } else if (opt.start_host) {
        int rc = session_begin(&ses, &opt, opt.start_host, opt.password, 1);
        if (rc == 0) {
            snprintf(msg, sizeof(msg), "Streaming from C64 at %s", opt.start_host);
        } else {
            session_end(&ses, &opt);
            connect_error(msg, sizeof(msg), rc, opt.start_host);
            want_picker = 1;
        }
    } else if (opt.remote_host) {
        session_begin(&ses, &opt, opt.remote_host, opt.password, 0);
        snprintf(msg, sizeof(msg), "Controlling the C64 at %s", opt.remote_host);
    } else if (opt.listen_only) {
        snprintf(msg, sizeof(msg), "Waiting for streams on ports %d and %d", opt.port, opt.audio_port);
    } else {
#ifdef __PROSPERO__
        want_picker = 1;    /* first run: nothing configured yet */
#endif
        snprintf(msg, sizeof(msg), "Waiting for streams on ports %d and %d", opt.port, opt.audio_port);
    }
    if (opt.no_home)
        notify(msg);
    if (want_picker) {
        ui_open_picker(&ui, opt.start_host ? msg : NULL);
        scan = discover_start(opt.start_host, opt.password);
    }
    if (opt.ui_screen && !strcmp(opt.ui_screen, "menu"))
        ui_open_menu(&ui);
    if (opt.ui_screen && !strcmp(opt.ui_screen, "assembly64"))
        ui_open_a64(&ui);
    if (opt.ui_screen && !strcmp(opt.ui_screen, "about"))
        ui_open_about(&ui);
    if (opt.ui_screen && !strncmp(opt.ui_screen, "favourites", 10)) {
        ui_open_favs(&ui);
        if (!strcmp(opt.ui_screen, "favourites-games"))
            ui_input(&ui, IN_OK);
    }
    if (opt.ui_screen && !strcmp(opt.ui_screen, "recents"))
        ui_open_recents(&ui);
#ifdef PS64_SID
    if (opt.ui_screen && !strcmp(opt.ui_screen, "sid")) {
        ui_action sa = ui_open_sid(&ui);
        sidplay_songlengths(opt.songlengths);
        sid_list(&ui, sa.path);
        if (sp.engine)
            sid_show(&ui);
    }
#endif
    if (opt.browse_path && ses.files) {
        ui_action a = ui_open_browser(&ui, opt.browse_path);
        files_list(ses.files, a.path);
    }
#ifdef PS64_EMU
    snprintf(ui.games_dir, sizeof(ui.games_dir), "%s", games_dir);
    char emu_browse[512] = "/";       /* where the emulator's browser opens */
    if (opt.browse_path && ses.emu) {
        ui_open_browser(&ui, opt.browse_path);
        emu_list(&ui, opt.browse_path);
    }
    if (opt.emu_load && ses.emu) {
        eg_launch_path(&eg, opt.emu_load, FA_NONE, msg, sizeof(msg));
        printf("%s\n", msg);
    }
#endif
    ctl.remote = ses.rem;
    snprintf(ui.current_ip, sizeof(ui.current_ip), "%s", ses.host);
    int typed = 0;
    /* Assembly64 (with PS64's own client ID). */
    a64 *a64c = a64_start(opt.a64_url);
    if (a64c) {
        a64_load_categories(a64c);
        ui.a64_ready = 1;
    } else {
        fprintf(stderr, "assembly64: bad URL %s\n", opt.a64_url);
    }
    if (opt.a64_search && a64c) {
        ui_open_a64(&ui);
        snprintf(ui.a64_query, sizeof(ui.a64_query), "%s", opt.a64_search);
        ui_action dbg = ui_input(&ui, IN_R2);
        ctl.actions[ctl.nactions++] = dbg;
    }
    if (opt.a64_latest && a64c) {
        /* As if R1 was pressed with that category chosen (the list may not be loaded yet). */
        ui_open_a64(&ui);
        ui_action dbg = ui_input(&ui, IN_R1);
        snprintf(dbg.category_key, sizeof(dbg.category_key), "%s", opt.a64_latest);
        snprintf(ui.a64_search_cat, sizeof(ui.a64_search_cat), "%s", opt.a64_latest);
        ctl.actions[ctl.nactions++] = dbg;
    }

    /* Screenshots of Assembly64 results, from CSDb. */
    shots *shotw = shots_start();
    uint32_t *shot_px = NULL;           /* the one shown (owned here, handed to the UI) */
    int *shot_delays = NULL;
    char shot_id[64] = "";

    /* Assembly64 item waiting for its file list before it starts. */
    char auto_item[64] = "";
    int auto_category = 0;
    fav_entry auto_entry;
    memset(&auto_entry, 0, sizeof(auto_entry));

    int index_was_building = 0;
    Uint32 search_refresh_at = 0, build_report_at = 0;
    if (opt.build_index && ses.index) {
#ifdef PS64_EMU
        if (ses.emu)
            index_build_local_start(ses.index, games_dir, emu_index_file);
        else
#endif
        index_build_start(ses.index, ses.host, ses.api.password, opt.search_roots, index_file);
        index_was_building = 1;
    }
    if (opt.search_text && ses.index) {
        ui.index = ses.index;
        ui_open_search(&ui);
        ui_text(&ui, opt.search_text);
        ui.search_focus = 1;
    }
    if (opt.debug_connect) {
        ui_action dbg = { .kind = UA_CONNECT };
        snprintf(dbg.ip, sizeof(dbg.ip), "%s", opt.debug_connect);
        ctl.actions[ctl.nactions++] = dbg;
    }
    if (opt.launch_fav > 0 && opt.launch_fav <= favs.count) {
        ui_action dbg = { .kind = UA_FAV_LAUNCH };
        dbg.fav = favs.e[opt.launch_fav - 1];
        ctl.actions[ctl.nactions++] = dbg;
    }

    for (int i = 0; i < SDL_NumJoysticks(); i++)
        SDL_GameControllerOpen(i);

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (controls_event(&ctl, &ev))
                continue;       /* handled by the controls */

            if (ev.type == SDL_QUIT)
                running = 0;
            else if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE: case SDLK_q: running = 0; break;
                case SDLK_f:
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    break;
                case SDLK_m:
                    if (opt.audio) {
                        audio_set_muted(&aout, !aout.muted);
                        ui.muted = aout.muted;
                        printf("audio %s\n", aout.muted ? "muted" : "on");
                    }
                    break;
                case SDLK_n:
                    dec.swap_nibbles = !dec.swap_nibbles;
                    printf("swap nibbles: %s\n", dec.swap_nibbles ? "on" : "off");
                    break;
                case SDLK_s: {
                    char path[64];
                    snprintf(path, sizeof(path), "ps64-%u.bmp", (unsigned)SDL_GetTicks());
                    if (dec.stats.frames) save_snapshot(&dec, path);
                    break;
                }
                }
            }
        }

        /* Drain everything that arrived since the last vsync. */
        for (;;) {
            ssize_t n = recv(sock, buf, sizeof(buf), 0);
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                    perror("recv");
                break;
            }
            if (!ses.emu && vic_feed(&dec, buf, (size_t)n))
                dirty = 1;
        }
        while (opt.audio) {
            ssize_t n = recv(asock, buf, sizeof(buf), 0);
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                    perror("recv audio");
                break;
            }
            if (SDL_TICKS_PASSED(SDL_GetTicks(), audio_from) == 0 || ses.emu || ui.screen == UI_HOME || sid_playing)
                continue;   /* (the home screen is silent) */
            size_t frames = audio_feed(&adec, buf, (size_t)n, samples, AUDIO_MAX_FRAMES);
            if (frames)
                audio_push(&aout, samples, frames);
#ifdef PS64_SID
            if (frames && sp.on_ultimate)           /* the SID player's scope, from the Ultimate */
                sid_scope_add(samples, (int)frames);
#endif
        }

#ifdef PS64_EMU
        if (ses.emu && emu_step(&aout, opt.audio)) {
            int w, h;
            const uint32_t *px = emulator_frame(&w, &h);
            if (px) {
                if (!tex || tex_w != w || tex_h != h) {
                    if (tex) SDL_DestroyTexture(tex);
                    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                        SDL_TEXTUREACCESS_STREAMING, w, h);
                    tex_w = w;
                    tex_h = h;
                    SDL_RenderSetLogicalSize(ren, tex_w, tex_h);
                }
                SDL_UpdateTexture(tex, NULL, px, w * 4);
                emu_frames++;
            }
        }
#endif
        if (dirty) {
            if (!tex || tex_w != dec.width || tex_h != dec.height) {
                if (tex) SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                    SDL_TEXTUREACCESS_STREAMING, dec.width, dec.height);
                tex_w = dec.width;
                tex_h = dec.height;
                SDL_RenderSetLogicalSize(ren, tex_w, tex_h);
            }
            void *pix;
            int pitch;
            if (SDL_LockTexture(tex, NULL, &pix, &pitch) == 0) {
                vic_to_argb(&dec, pix, pitch);
                SDL_UnlockTexture(tex);
            }
            dirty = 0;

            /* Give the stream a moment to settle, then save and exit. */
            if (opt.snapshot && SDL_GetTicks() - started > 1500) {
                save_snapshot(&dec, opt.snapshot);
                running = 0;
            }
        }

        Uint32 tick = SDL_GetTicks();
        /* Debug: the --keys buttons, one at a time. */
        if (opt.keys && *opt.keys && tick - started > 2000) {
            static Uint32 key_at;
            if (!key_at || tick - key_at >= 1500) {
                static const char *const names[] = { "up", "down", "left", "right", "ok", "back", "square",
                                                     "triangle", "l1", "r1", "l2", "r2" };
                static const int ins[] = { IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_OK, IN_BACK, IN_ALT,
                                           IN_TRIANGLE, IN_L1, IN_R1, IN_L2, IN_R2 };
                size_t len = strcspn(opt.keys, ",");
                for (size_t k = 0; k < sizeof(ins) / sizeof(ins[0]); k++)
                    if (strlen(names[k]) == len && !strncmp(opt.keys, names[k], len) && ctl.nactions < ACTIONQ) {
                        printf("keys: %s\n", names[k]);
                        ctl.actions[ctl.nactions++] = ui_input(&ui, ins[k]);
                    }
                opt.keys += len + (opt.keys[len] == ',');
                key_at = tick;
            }
        }
        controls_update(&ctl, tick);

        /* Act on menu, picker and browser choices. */
        ui_action a;
        while (controls_take_action(&ctl, &a)) {
            switch (a.kind) {
            case UA_QUIT:
                running = 0;
                break;
            case UA_MUTE:
                if (opt.audio) {
                    audio_set_muted(&aout, !aout.muted);
                    ui.muted = aout.muted;
                }
                break;
            case UA_VOLUME:
                if (opt.audio) {
                    int muted = aout.muted, *db = ses.emu ? &opt.emulator_volume : &opt.stream_volume;
                    const char *key = ses.emu ? "emulator-volume" : "stream-volume";
                    if (sid_playing) {
                        db = &opt.sid_volume;
                        key = "sid-volume";
                    }
                    ui_volume_step(db, &muted, a.offset);
                    if (muted != aout.muted)
                        audio_set_muted(&aout, muted);
                    ui.muted = aout.muted;
                    if (config) {
                        char v[16];
                        snprintf(v, sizeof(v), "%d", *db);
                        config_set(config, key, v, NULL);
                    }
                }
                break;
            case UA_PORT:
                ctl.port = ctl.port == 1 ? 2 : 1;
                ui.port = ctl.port;
                break;
            case UA_RESCAN:
                discover_stop(scan);
                ui.ndevices = 0;
                ui.scanning = 1;
                scan = discover_start(ses.host[0] ? ses.host : NULL, opt.password);
                break;
            case UA_CONNECT: {
                discover_stop(scan);
                scan = NULL;
                session_end(&ses, &opt);
                int rc = session_begin(&ses, &opt, a.ip, a.password, 1);
                if (rc != 0) {
                    session_end(&ses, &opt);
                    connect_error(msg, sizeof(msg), rc, a.ip);
                    ui_open_picker(&ui, msg);
                    scan = discover_start(a.ip, NULL);
                    break;
                }
                if (config) {
                    static const char *const legacy[] = { "start", NULL };
                    config_set(config, "c64", a.ip, legacy);
                    if (a.password[0])
                        config_set(config, "password", a.password, NULL);
                }
                snprintf(home_want, sizeof(home_want), "%s", a.ip);
                snprintf(home_pass, sizeof(home_pass), "%s", a.password);
                home_locked = 0;
                stream_since = tick;
                snprintf(msg, sizeof(msg), "Connected to the C64 at %s", a.ip);
                notify(msg);
                if (ui.picker_from_home)
                    ui_open_home(&ui);          /* the 1084 shows it from now on */
                else
                    controls_banner(&ctl, msg);
                break;
            }
            case UA_HOME:
#ifdef PS64_SID
                sid_stop(&ui, ses.files);
#endif
                /* Back to the home screen: the emulator pauses where it is (Vice resumes it);
                 * a stream keeps running so the 1084 shows it. */
                if (ses.emu) {
                    session_end(&ses, &opt);
                    home_search_at = tick;
                }
                break;
            case UA_HOME_VICE:
#ifdef PS64_EMU
                discover_stop(scan);
                scan = NULL;
                session_end(&ses, &opt);
                if (session_begin_emulator(&ses, &opt) != 0) {
                    session_end(&ses, &opt);
                    ui_open_home(&ui);
                    snprintf(ui.home_snack, sizeof(ui.home_snack), "The built-in emulator didn't start");
                    ui.home_snack_serial++;
                    break;
                }
                ui_close(&ui);
                notify("Built-in emulator");
#else
                snprintf(ui.home_snack, sizeof(ui.home_snack), "This PS64 build has no emulator");
                ui.home_snack_serial++;
#endif
                break;
            case UA_HOME_STREAM:
                ui_close(&ui);
                break;
#ifdef PS64_SID
            case UA_HOME_SID:
            case UA_SID_LIST:
                if (a.kind == UA_HOME_SID) {
                    static int lengths_tried;
                    if (!lengths_tried) {           /* the song-length database, once */
                        lengths_tried = 1;
                        Uint32 t0 = SDL_GetTicks();
                        int rc = sidplay_songlengths(opt.songlengths);
                        printf("sid: song lengths %s (%u ms)\n", rc == 0 ? "loaded" : "not found",
                               SDL_GetTicks() - t0);
                    }
                }
                sid_list(&ui, a.path);
                break;
            case UA_SID_PLAY:
                sid_play_list(&ui, a.category, a.offset, a64c, ses.files, tick);
                break;
            case UA_SID_SONG:
                if (sp.engine) {
                    int song = ui.sid_song + a.offset;
                    if (song < 1 || song > ui.sid_songs)
                        break;
                    sidplay_select(sp.engine, song);
                    if (sp.on_ultimate && ses.files && sp.data) {
                        unsigned char *copy = malloc(sp.len);
                        if (copy) {
                            memcpy(copy, sp.data, sp.len);
                            files_play_sid(ses.files, sp.name, copy, sp.len, song);
                        }
                        sp.ult_started = tick;
                    }
                    sid_show(&ui);
                }
                break;
            case UA_SID_SKIP:
                if (sp.n && (a.offset < 0 ? playlist_prev(&sp.list) : playlist_next(&sp.list, 1, 1)) >= 0)
                    sid_load_current(&ui, a64c, ses.files);
                break;
            case UA_SID_PAUSE:
                if (sp.on_ultimate)
                    snprintf(ui.sid_status, sizeof(ui.sid_status), "The C64 Ultimate can't pause a tune");
                else if (sp.engine)
                    sp.paused = !sp.paused;
                sid_show(&ui);
                break;
            case UA_SID_MODE:
                ui.sid_mode = (ui.sid_mode + 1) % PLAY_MODES;
                if (sp.n)
                    playlist_set_mode(&sp.list, ui.sid_mode);
                break;
            case UA_SID_OUTPUT:
                sp.prefer_ultimate = !sp.prefer_ultimate;
                printf("sid: play on %s\n", sp.prefer_ultimate ? "the C64 Ultimate" : "PS64");
                opt.sid_ultimate = sp.prefer_ultimate;
                if (config)
                    config_set(config, "sid-output", sp.prefer_ultimate ? "ultimate" : "ps64", NULL);
                if (sp.engine && sp.data) {          /* the tune carries on at the other end */
                    if (sp.on_ultimate && ses.files)
                        files_action(ses.files, "", FA_RESET);
                    unsigned char *data = sp.data;
                    char name[160];
                    snprintf(name, sizeof(name), "%s", sp.name);
                    sp.data = NULL;
                    sid_play_bytes(&ui, ses.files, name, data, sp.len);
                }
                sid_show(&ui);
                break;
            case UA_SID_STOP:
                sid_stop(&ui, ses.files);
                break;
#endif
#ifdef PS64_EMU
            case UA_DISK: {
                int n = emulator_disk_count();
                if (n < 2)
                    break;
                int d = ((emulator_disk_index() + a.offset) % n + n) % n;
                if (emulator_disk_insert(d) == 0)
                    snprintf(msg, sizeof(msg), "Disk %d of %d inserted", d + 1, n);
                else
                    snprintf(msg, sizeof(msg), "Couldn't change the disk");
                controls_banner(&ctl, msg);
                break;
            }
#endif
            case UA_CONTROLLER:
                ctl.joy_keys = !ctl.joy_keys;
                if (config)
                    config_set(config, "dpad", ctl.joy_keys ? "keys" : "joystick", NULL);
                controls_banner(&ctl, ctl.joy_keys ? "D-pad: cursor keys, Cross: RETURN"
                                                   : "D-pad: joystick, Cross: fire");
                break;
            case UA_RESET:
            case UA_REBOOT:
#ifdef PS64_EMU
                if (ses.emu) {
                    eg_a64_cancel(&eg);
                    emulator_reset(a.kind == UA_REBOOT);
                    controls_banner(&ctl, a.kind == UA_RESET ? "C64 reset" : "C64 switched off and on");
                    break;
                }
#endif
                if (!ses.files) {
                    controls_banner(&ctl, "No C64 Ultimate connected (Options > Home > settings)");
                    break;
                }
                files_action(ses.files, "", a.kind == UA_RESET ? FA_RESET : FA_REBOOT);
                controls_banner(&ctl, a.kind == UA_RESET ? "Resetting the C64..." : "Rebooting the C64...");
                break;
            case UA_A64_SEARCH:
                if (a64c)
                    a64_search(a64c, a.text, a.category_key, a.offset);
                break;
            case UA_A64_LATEST:
                if (a64c)
                    a64_latest(a64c, a.category_key);
                break;
            case UA_A64_ENTRIES:
                if (a64c)
                    a64_entries(a64c, a.text, a.category);
                break;
            case UA_A64_FILE:
#ifdef PS64_EMU
                if (ses.emu && a64c) {
                    int chosen = -1;
                    for (int i = 0; i < ui.a64_nfiles; i++)
                        if (ui.a64_files[i].id == a.file_id)
                            chosen = i;
                    int rc = eg_a64_begin(&eg, a64c, a.text, a.category, ui.a64_cur.name, ui.a64_files,
                                          ui.a64_nfiles, chosen, a.file_action, msg, sizeof(msg));
                    if (rc == EG_WAITING) {
                        ui.a64_loading = 1;
                        snprintf(ui.a64_status, sizeof(ui.a64_status), "%s", msg);
                    } else {
                        if (rc == EG_STARTED)
                            ui_close(&ui);
                        controls_banner(&ctl, msg);
                    }
                    if (rc != EG_ERROR)
                        record_recent(&recents, fav_a64_file(a.text, a.category, a.file_id, a.path,
                                                             ui.a64_cur.name), a.file_action);
                    break;
                }
#endif
                if (!ses.files) {
                    controls_banner(&ctl, "No C64 Ultimate connected (Options > Home > settings)");
                    break;
                }
                if (a64c) {
                    a64_download(a64c, a.text, a.category, a.file_id, a.path, a.file_action);
                    ui.a64_loading = 1;
                    record_recent(&recents, fav_a64_file(a.text, a.category, a.file_id, a.path,
                                                         ui.a64_cur.name), a.file_action);
                }
                break;
            case UA_OPEN_SEARCH:
                if (!ses.index) {
                    controls_banner(&ctl, "No C64 Ultimate connected (Options > Home > settings)");
                    break;
                }
                ui.index = ses.index;
                ui_open_search(&ui);
                break;
            case UA_BUILD_INDEX:
                if (!ses.index) {
                    controls_banner(&ctl, "No C64 Ultimate connected (Options > Home > settings)");
                    break;
                }
#ifdef PS64_EMU
                if (ses.emu)
                    index_build_local_start(ses.index, games_dir, emu_index_file);
                else
#endif
                index_build_start(ses.index, ses.host, ses.api.password, opt.search_roots, index_file);
                index_was_building = 1;
                ui.index = ses.index;
                ui_open_search(&ui);
                controls_banner(&ctl, "Building the search index...");
                break;
            case UA_OPEN_BROWSER:
#ifdef PS64_EMU
                if (ses.emu) {
                    a = ui_open_browser(&ui, emu_browse);
                    emu_list(&ui, a.path);
                    break;
                }
#endif
                if (!ses.files) {
                    controls_banner(&ctl, "No C64 Ultimate connected (Options > Home > settings)");
                    break;
                }
                a = ui_open_browser(&ui, browse_dir);
                files_list(ses.files, a.path);
                break;
            case UA_LIST:
#ifdef PS64_EMU
                if (ses.emu)
                    emu_list(&ui, a.path);
#endif
                if (ses.files)
                    files_list(ses.files, a.path);
                break;
            case UA_FAV_TOGGLE: {
                int added = fav_toggle(&favs, &a.fav);
                fav_save(&favs, favs_file);
                ui_favs_changed(&ui);
                snprintf(msg, sizeof(msg), "%s favourites: %s", added ? "Added to" : "Removed from",
                         a.fav.name);
                controls_banner(&ctl, msg);
                break;
            }
            case UA_FAV_LAUNCH: {
                fav_entry *e = &a.fav;
                int local = fav_is_path(e);
                if (local && fav_is_emulator(e) != ses.emu) {
                    controls_banner(&ctl, ses.emu ? "That's on a C64 Ultimate (Options > Home > Stream)"
                                                  : "That's in the built-in emulator's games folder "
                                                    "(Options > Home > Vice)");
                    break;
                }
#ifdef PS64_EMU
                if (ses.emu) {
                    if (!local && !a64c) {
                        controls_banner(&ctl, "Assembly64 isn't available (check assembly64-url)");
                        break;
                    }
                    int rc = EG_WAITING;
                    if (local) {
                        rc = eg_launch_path(&eg, e->path, fav_is_folder(e) ? FA_LAUNCH_FOLDER : e->action,
                                            msg, sizeof(msg));
                    } else {
                        /* Fetch the item's files first: the start may need its other disks. */
                        snprintf(auto_item, sizeof(auto_item), "%s", e->item_id);
                        auto_category = e->category;
                        auto_entry = *e;
                        a64_entries(a64c, e->item_id, e->category);
                        snprintf(msg, sizeof(msg), "Starting %s...", e->name);
                    }
                    if (rc != EG_ERROR) {
                        record_recent(&recents, *e, e->action);
                        ui_close(&ui);
                    }
                    controls_banner(&ctl, msg);
                    break;
                }
#endif
                if (!ses.files) {
                    controls_banner(&ctl, "No C64 Ultimate connected (Options > Home > settings)");
                    break;
                }
                if (!local && !a64c) {
                    controls_banner(&ctl, "Assembly64 isn't available (check assembly64-url)");
                    break;
                }
                int action = e->action != FA_NONE ? e->action
                           : e->kind == FAV_LOCAL_FOLDER || e->kind == FAV_A64_ITEM ? FA_LAUNCH_FOLDER
                           : files_default_action(files_type(e->path, 0));
                if (e->kind == FAV_LOCAL_FILE || e->kind == FAV_LOCAL_FOLDER) {
                    files_action(ses.files, e->path, action);
                } else if (e->kind == FAV_A64_FILE) {
                    a64_download(a64c, e->item_id, e->category, e->file_id, e->path, action);
                } else {
                    /* Fetch the item's files, then start the first runnable one. */
                    snprintf(auto_item, sizeof(auto_item), "%s", e->item_id);
                    auto_category = e->category;
                    auto_entry = *e;
                    a64_entries(a64c, e->item_id, e->category);
                }
                record_recent(&recents, *e, e->action);
                ui_close(&ui);
                snprintf(msg, sizeof(msg), "Starting %s...", e->name);
                controls_banner(&ctl, msg);
                break;
            }
            case UA_FILE:
#ifdef PS64_EMU
                if (ses.emu) {
                    if (eg_launch_path(&eg, a.path, a.file_action, msg, sizeof(msg)) != EG_ERROR) {
                        record_recent(&recents, fav_local(a.path, 0), a.file_action);
                        ui_close(&ui);
                        ui_parent(a.path, emu_browse, sizeof(emu_browse));
                    }
                    controls_banner(&ctl, msg);
                    break;
                }
#endif
                if (!ses.files)
                    break;
                files_action(ses.files, a.path, a.file_action);
                record_recent(&recents, fav_local(a.path, 0), a.file_action);
                ui_close(&ui);
                ui_parent(a.path, browse_dir, sizeof(browse_dir));
                if (config)
                    config_set(config, "browse-dir", browse_dir, NULL);
                controls_banner(&ctl, "Starting...");
                break;
            }
        }
        ctl.remote = ses.rem;
        ui.index = ses.index;
        snprintf(ui.current_ip, sizeof(ui.current_ip), "%s", ses.host);
        ui.emulator = ses.emu;
        ui.joy_keys = ctl.joy_keys;
#ifdef PS64_EMU
        ui.disks = ses.emu ? emulator_disk_count() : 0;
        ui.disk = ses.emu ? emulator_disk_index() : 0;
#endif
        /* Switched machine: drop the old picture, play sound at the new one's rate. */
        if (ses.emu != was_emu) {
            was_emu = ses.emu;
            if (tex) {
                SDL_DestroyTexture(tex);
                tex = NULL;
            }
            vic_init(&dec);
            dec.swap_nibbles = opt.swap_nibbles;
        }
#ifdef PS64_EMU
        int want_rate = ses.emu ? emulator_sample_rate() : opt.audio_rate;
#else
        int want_rate = opt.audio_rate;
#endif
#ifdef PS64_SID
        sid_playing = sp.engine && !sp.on_ultimate;
        if (sid_playing)
            want_rate = SIDPLAY_RATE;
#endif
        if (opt.audio && want_rate != aout_rate) {
            audio_reopen(&aout, want_rate, opt.audio_latency_ms);
            aout_rate = want_rate;
        }
        /* The home screen's view of the stream: live while pictures arrive. A stream that
         * stopped (the C64 switched off) is dropped after 10 s and looked for again. */
        if (dec.stats.frames > frames_seen)
            last_frame_tick = tick;
        frames_seen = dec.stats.frames;
        int live = !ses.emu && last_frame_tick && tick - last_frame_tick < 1500;
        if (ui.screen == UI_HOME && ses.streaming && !live &&
            tick - (last_frame_tick > stream_since ? last_frame_tick : stream_since) > 10000) {
            session_end(&ses, &opt);
            home_search_at = tick;
        }
        if (ses.streaming)
            snprintf(ui.home_ip, sizeof(ui.home_ip), "%s", ses.host);
        ui.home_stream = live ? HOME_STREAM_LIVE : ses.streaming ? HOME_STREAM_CONNECTING
                       : home_locked ? HOME_STREAM_LOCKED : scan ? HOME_STREAM_SEARCHING
                       : HOME_STREAM_NONE;

        /* Each source keeps its own level (both start at -12 dB). */
        ui.volume_db = ses.emu ? opt.emulator_volume : opt.stream_volume;
#ifdef PS64_SID
        /* The SID player: sound, time, the visualiser, and on to the next tune at the end. */
        ui_sid_set_ultimate(&ui, ses.streaming && ses.files);
        if (sp.engine && !sp.on_ultimate) {
            ui.volume_db = opt.sid_volume;
            if (opt.audio && !aout.muted && !sp.paused)
                sid_step(&aout, sp.engine);
            else if (!sp.paused)
                sid_step_silent(sp.engine, tick);
            ui.sid_time_ms = sidplay_time_ms(sp.engine);
            sidplay_regs(sp.engine, 0, ui.sid_regs);
            memcpy(ui.sid_scope, sp.paused ? (int16_t[SID_SCOPE]){0} : sp.scope, sizeof(ui.sid_scope));
            static Uint32 sid_log_at;
            if (!sid_log_at)
                sid_log_at = tick;
            if (tick - sid_log_at >= 10000) {
                printf("sid: rendering took %.1f%% of the time\n", sid_render_ms * 100.0 / (tick - sid_log_at));
                sid_render_ms = 0;
                sid_log_at = tick;
            }
        } else if (sp.engine) {
            /* (A tune started after this frame's tick: not a negative, i.e. huge, time.) */
            ui.sid_time_ms = SDL_TICKS_PASSED(tick, sp.ult_started) ? tick - sp.ult_started : 0;
            memcpy(ui.sid_scope, sp.scope, sizeof(ui.sid_scope));     /* from the stream's sound */
        }
        if (sp.engine && sp.n && !sp.paused) {
            unsigned len = ui.sid_length_ms > 0 ? (unsigned)ui.sid_length_ms : SID_DEFAULT_MS;
            if (ui.sid_time_ms >= len && playlist_next(&sp.list, 0, 1) >= 0)
                sid_load_current(&ui, a64c, ses.files);
        }
#endif
        if (opt.audio)
            audio_set_volume(&aout, ui.volume_db);

        /* Search index: refresh results while it builds, report when done. */
        if (ses.index && index_was_building) {
            index_status st;
            index_status_get(ses.index, &st);
            if (opt.build_index && SDL_TICKS_PASSED(tick, build_report_at)) {
                printf("indexing: %d folders, %d files, %d failed  %s\n",
                       st.dirs, st.files, st.failed_dirs, st.current);
                build_report_at = tick + 1000;
            }
            if (st.building && ui.screen == UI_SEARCH && SDL_TICKS_PASSED(tick, search_refresh_at)) {
                ui_search_refresh(&ui);
                search_refresh_at = tick + 700;
            }
            if (!st.building) {
                index_was_building = 0;
                if (st.done)
                    snprintf(msg, sizeof(msg), "Search index ready: %d files in %d folders",
                             st.files, st.dirs);
                else
                    snprintf(msg, sizeof(msg), "Indexing stopped: %s",
                             st.error[0] ? st.error : "cancelled");
                notify(msg);
                controls_banner(&ctl, msg);
                if (ui.screen == UI_SEARCH)
                    ui_search_refresh(&ui);
                if (opt.build_index) {
                    printf("index: %d files, %d folders, %d failed, %.1f s -> %s\n",
                           st.files, st.dirs, st.failed_dirs, (tick - started) / 1000.0,
#ifdef PS64_EMU
                           ses.emu ? emu_index_file :
#endif
                           index_file);
                    running = 0;
                }
            }
        }

        /* The highlighted Assembly64 result's screenshot, from CSDb (CSDb releases only). */
        if (ui.screen == UI_A64 && ui.a64_focus == 1 && ui.a64_sel < ui.a64_count) {
            const a64_item *it = &ui.a64_items[ui.a64_sel];
            if (strcmp(shot_id, it->id)) {
                snprintf(shot_id, sizeof(shot_id), "%s", it->id);
                free(shot_px);
                free(shot_delays);
                shot_px = NULL;
                shot_delays = NULL;
                ui.a64_shot = NULL;
                ui.a64_shot_delays = NULL;
                if (shotw && a64_is_csdb(&ui.a64_cats, it->category)) {
                    ui.a64_shot_state = SHOT_LOADING;
                    shots_want(shotw, it->id);
                } else {
                    ui.a64_shot_state = SHOT_NONE;
                }
            }
            if (ui.a64_shot_state == SHOT_LOADING) {
                uint32_t *px;
                int w, h, frames, *delays;
                int got = shots_fetch(shotw, shot_id, &px, &w, &h, &frames, &delays);
                if (got == 1) {
                    shot_px = px;
                    shot_delays = delays;
                    ui.a64_shot = px;
                    ui.a64_shot_frames = frames;
                    ui.a64_shot_delays = delays;
                    ui.a64_shot_w = w;
                    ui.a64_shot_h = h;
                    ui.a64_shot_serial++;
                    ui.a64_shot_state = SHOT_READY;
                } else if (got < 0) {
                    ui.a64_shot_state = SHOT_NONE;
                }
            }
        }

        if (a64c) {
            a64_result r;
            while (a64_poll(a64c, &r)) {
#ifdef PS64_SID
                /* The SID player's tune: its file list, then the .sid itself (kept in the cache). */
                if (r.kind == A64R_ENTRIES && sp.want_id[0] && !strcmp(r.item_id, sp.want_id) &&
                    r.category == sp.want_cat) {
                    int pick = -1;
                    for (int i = 0; i < r.nfiles && pick < 0; i++)
                        if (files_type(r.files[i].path, 0) == FT_SID)
                            pick = i;
                    if (pick >= 0) {
                        a64_download(a64c, r.item_id, r.category, r.files[pick].id, r.files[pick].path, SID_FA);
                    } else {
                        snprintf(ui.sid_status, sizeof(ui.sid_status), "No .sid file in that release");
                        sp.want_id[0] = 0;
                    }
                    a64_result_free(&r);
                    continue;
                }
                if (r.kind == A64R_FILE && r.action == SID_FA) {
                    int cur = playlist_current(&sp.list);
                    if (sp.want_id[0] && sp.tab == SID_TAB_A64 && cur >= 0 &&
                        !strcmp(sp.items[cur].id, sp.want_id)) {
                        char path[700];
                        sid_cache_path(&sp.items[cur], path, sizeof(path));
                        localfs_write(path, r.data, r.len);
                        sp.want_id[0] = 0;
                        sid_play_bytes(&ui, ses.files, sp.items[cur].name, r.data, r.len);
                        r.data = NULL;                  /* the player has it */
                    }
                    a64_result_free(&r);
                    continue;
                }
                if (r.kind == A64R_ERROR && sp.want_id[0]) {
                    snprintf(ui.sid_status, sizeof(ui.sid_status), "%s", r.message);
                    sp.want_id[0] = 0;
                }
#endif
                if (r.kind == A64R_FILE) {
                    ui.a64_loading = 0;
#ifdef PS64_EMU
                    int erc = ses.emu ? eg_a64_file(&eg, a64c, &r, msg, sizeof(msg)) : EG_ERROR;
                    if (ses.emu) {
                        if (erc == EG_WAITING) {
                            ui.a64_loading = 1;
                            snprintf(ui.a64_status, sizeof(ui.a64_status), "%s", msg);
                        } else if (erc == EG_STARTED) {
                            ui_close(&ui);
                        }
                        if (msg[0])
                            controls_banner(&ctl, msg);
                    } else
#endif
                    if (ses.files) {
                        files_action_data(ses.files, r.name, r.data, r.len, r.action);
                        r.data = NULL;              /* handed to the file worker */
                        ui_close(&ui);
                        snprintf(msg, sizeof(msg), "Downloaded %s - starting", r.name);
                    } else {
                        snprintf(msg, sizeof(msg), "Downloaded %s, but no C64 is chosen", r.name);
                    }
                    controls_banner(&ctl, msg);
                } else if (r.kind == A64R_ERROR) {
                    ui_a64_error(&ui, r.message, r.auth_error);
                    if (ui.screen != UI_A64 && ui.screen != UI_A64_FILES)
                        controls_banner(&ctl, r.message);
                } else if (r.kind == A64R_ENTRIES && auto_item[0] &&
                           !strcmp(r.item_id, auto_item) && r.category == auto_category) {
                    /* A favourite/recent Assembly64 item: start its first runnable file. */
                    auto_item[0] = 0;
                    const char *names[256];
                    int nn = r.nfiles < 256 ? r.nfiles : 256;
                    for (int i = 0; i < nn; i++)
                        names[i] = r.files[i].path;
                    int pick = files_pick_launch(names, nn);
                    if (auto_entry.kind == FAV_A64_FILE)
                        for (int i = 0; i < nn; i++)
                            if (r.files[i].id == auto_entry.file_id)
                                pick = i;
                    if (pick < 0) {
                        snprintf(msg, sizeof(msg), "Nothing to run in %s", auto_entry.name);
                        controls_banner(&ctl, msg);
#ifdef PS64_EMU
                    } else if (ses.emu) {
                        int rc = eg_a64_begin(&eg, a64c, r.item_id, r.category, auto_entry.name, r.files,
                                              r.nfiles, pick, auto_entry.action, msg, sizeof(msg));
                        controls_banner(&ctl, msg);
                        (void)rc;
#endif
                    } else {
                        const a64_file *f = &r.files[pick];
                        a64_download(a64c, r.item_id, r.category, f->id, f->path,
                                     files_default_action(files_type(f->path, 0)));
                    }
                } else {
                    ui_a64_results(&ui, &r);
                }
                a64_result_free(&r);
            }
        }

        /* Home: look for the C64 Ultimate (again every 15 s until one answers). */
        int home_idle = ui.screen == UI_HOME && !ses.streaming && !ses.emu;
        if (home_idle && !scan && SDL_TICKS_PASSED(tick, home_search_at))
            scan = discover_start(home_want[0] ? home_want : NULL, home_pass);
        if (scan) {
            c64_device found[DISCOVER_MAX];
            int n, progress;
            int done = discover_poll(scan, found, &n, &progress);
            ui_set_devices(&ui, found, n, progress, done);
            /* On the home screen, start the saved C64's stream as soon as it answers (or the
             * only one found, the first time). */
            const c64_device *pick = NULL;
            for (int i = 0; home_idle && i < n; i++)
                if (home_want[0] && !strcmp(found[i].ip, home_want))
                    pick = &found[i];
            if (home_idle && !pick && !home_want[0] && done && n == 1)
                pick = &found[0];
            if (pick) {
                char ip[16];
                snprintf(ip, sizeof(ip), "%s", pick->ip);
                snprintf(ui.home_ip, sizeof(ui.home_ip), "%s", ip);
                home_locked = pick->locked && !home_pass[0];
                discover_stop(scan);
                scan = NULL;
                done = 0;
                home_search_at = tick + 15000;
                if (!home_locked) {
                    int rc = session_begin(&ses, &opt, ip, home_pass, 1);
                    if (rc == 0) {
                        stream_since = tick;
                        if (config && strcmp(home_want, ip)) {
                            static const char *const legacy[] = { "start", NULL };
                            config_set(config, "c64", ip, legacy);
                        }
                        snprintf(home_want, sizeof(home_want), "%s", ip);
                    } else {
                        session_end(&ses, &opt);
                        home_locked = rc == 403;
                    }
                }
            }
            if (done) {
                discover_stop(scan);
                scan = NULL;
                if (home_idle)
                    home_search_at = tick + 15000;
            }
        }

        if (ses.files) {
            files_result fr;
            while (files_poll(ses.files, &fr)) {
                int browsing = ui.screen == UI_BROWSER || ui.screen == UI_ACTIONS;
                if (fr.kind == FR_LISTING && browsing)
                    ui_set_listing(&ui, fr.path, fr.entries, fr.count);
                else if (fr.kind == FR_ERROR && browsing && ui.loading)
                    ui_set_list_error(&ui, fr.path, fr.message);
                else {
                    free(fr.entries);
                    if (fr.message[0]) {
                        notify(fr.message);
                        controls_banner(&ctl, fr.message);
                    }
                }
            }
        }

#ifdef PS64_EMU
        if (ses.emu) {
            char emsg[96];
            while (emulator_take_message(emsg, sizeof(emsg)))
                controls_banner(&ctl, emsg);
        }
#endif
        if (ses.rem) {
            char rmsg[96];
            while (remote_take_message(ses.rem, rmsg, sizeof(rmsg))) {
                notify(rmsg);
                controls_banner(&ctl, rmsg);
            }
            int m = remote_mode(ses.rem);
            if (opt.type_text && !typed && m != REMOTE_DETECTING) {
                type_text(ses.rem, opt.type_text);
                typed = 1;
            }
        }

        if (ui.screen == UI_HOME) {
            home_draw(ren, &ui, tex, tick);
        } else if (tex) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);
        } else {
            draw_waiting(ren);
        }
        controls_draw(&ctl, ren, tick);
        if (ui.screen == UI_HOME) {
            /* How fast the home screen draws (the PS5 draws it on the CPU). */
            static Uint32 home_fps_at, home_frames;
            home_frames++;
            if (!home_fps_at)
                home_fps_at = tick;
            if (tick - home_fps_at >= 10000) {
                printf("home: %.1f fps\n", home_frames * 1000.0 / (tick - home_fps_at));
                home_fps_at = tick;
                home_frames = 0;
            }
        }
        if (opt.screenshot && tick - started > (Uint32)opt.screenshot_ms) {
            int w, h;
            SDL_GetRendererOutputSize(ren, &w, &h);
            SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            if (shot && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888,
                                             shot->pixels, shot->pitch) == 0)
                printf("%s %s\n", SDL_SaveBMP(shot, opt.screenshot) == 0 ? "saved" : "failed to save",
                       opt.screenshot);
            SDL_FreeSurface(shot);
            running = 0;
        }
        SDL_RenderPresent(ren);   /* blocks on vsync */
#ifndef PS64_NATIVE
        /* (The native app's present already waits for the display.) */
        if (!has_vsync)
            SDL_Delay(4);         /* don't spin; packets wait in the socket buffer */
#endif

        Uint32 now = SDL_GetTicks();
        if (now - stats_at >= 1000) {
            char title[256], atitle[96] = "";
            double fps = (double)(dec.stats.frames - frames_at) * 1000.0 / (now - stats_at);
            if (opt.audio)
                snprintf(atitle, sizeof(atitle), "  |  audio %s lost %llu  underruns %llu  dropped %llu",
                    aout.muted ? "muted" : adec.stats.packets ? "on" : "waiting",
                    (unsigned long long)adec.stats.lost_packets,
                    (unsigned long long)aout.underruns,
                    (unsigned long long)aout.overflows);
            if (ses.emu)
                snprintf(title, sizeof(title), "PS64 - built-in emulator  %.1f fps",
                         (double)(emu_frames - frames_at) * 1000.0 / (now - stats_at));
            else
                snprintf(title, sizeof(title),
                    "C64 Ultimate - %dx%d  %.1f fps  lost %llu  bad %llu%s",
                    dec.width, dec.height, fps,
                    (unsigned long long)dec.stats.lost_packets,
                    (unsigned long long)dec.stats.bad_packets, atitle);
            SDL_SetWindowTitle(win, title);
            stats_at = now;
            frames_at = ses.emu ? emu_frames : dec.stats.frames;
        }
    }

    printf("video: %llu frames, %llu packets, %llu lost, %llu bad\n",
           (unsigned long long)dec.stats.frames, (unsigned long long)dec.stats.packets,
           (unsigned long long)dec.stats.lost_packets, (unsigned long long)dec.stats.bad_packets);
    if (opt.audio)
        printf("audio: %llu packets, %llu lost, %llu bad, %llu underruns, %llu dropped\n",
               (unsigned long long)adec.stats.packets, (unsigned long long)adec.stats.lost_packets,
               (unsigned long long)adec.stats.bad_packets,
               (unsigned long long)aout.underruns, (unsigned long long)aout.overflows);

    discover_stop(scan);
    a64_stop(a64c);
    shots_stop(shotw);
    free(shot_px);
    free(shot_delays);
    fav_free(&favs);
    fav_free(&recents);
    session_end(&ses, &opt);
#ifdef PS64_EMU
    emulator_stop();
#endif
#ifdef PS64_SID
    sid_stop(&ui, ses.files);
#endif
    ui_free(&ui);
    if (opt.audio)
        SDL_CloseAudioDevice(aout.dev);
    if (asock >= 0)
        close(asock);
    if (tex) SDL_DestroyTexture(tex);
    home_cleanup();
    font_cleanup();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    close(sock);
    return 0;
}
