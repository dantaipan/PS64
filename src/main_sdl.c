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
#include "osk_sdl.h"
#include "settings.h"
#include "ui.h"
#include "vicstream.h"

#include <SDL2/SDL.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
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
    const char *a64_id;       /* Assembly64 client ID */
    const char *a64_url;      /* Assembly64 API base URL */
    const char *a64_search;   /* debug: search Assembly64 for this at launch */
    int         launch_fav;   /* debug: launch this favourite (1-based) at startup */
} options;

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [--port N] [--c64 C64_IP|none] [--remote C64_IP] [--password PW] [--pick]\n"
        "          [--multicast GROUP] [--scale N] [--swap-nibbles]\n"
        "          [--snapshot FILE.bmp] [--screenshot FILE.bmp] [--osk]\n"
        "          [--no-audio] [--audio-port N] [--audio-multicast GROUP]\n"
        "          [--audio-rate HZ] [--audio-latency MS] [--config FILE]\n"
        "          [--joystick-port 1|2] [--button-NAME KEY] [--type TEXT] [--browse-dir DIR]\n"
        "          [--search-roots /SD,/USB0] [--build-index] [--search TEXT]\n"
        "          [--assembly64-client-id ID] [--assembly64-url URL] [--a64-search TEXT]\n"
        "\n"
        "--c64 (alias --start) streams from and controls that C64; --remote only controls it.\n"
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
    o->a64_id = NULL;
    o->a64_url = A64_DEFAULT_URL;
    o->a64_search = NULL;
    o->launch_fav = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *next = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--port") && next)           { o->port = atoi(next); i++; }
        else if ((!strcmp(a, "--start") || !strcmp(a, "--c64")) && next) {
            o->listen_only = !strcasecmp(next, "none");
            o->start_host = o->listen_only ? NULL : next;
            i++;
        }
        else if (!strcmp(a, "--pick"))              { o->pick = 1; }
        else if (!strcmp(a, "--browse-dir") && next) { o->browse_dir = next; i++; }
        else if (!strcmp(a, "--browse") && next)    { o->browse_path = next; i++; }
        else if (!strcmp(a, "--ui") && next)        { o->ui_screen = next; i++; }
        else if (!strcmp(a, "--screenshot-ms") && next) { o->screenshot_ms = atoi(next); i++; }
        else if (!strcmp(a, "--debug-connect") && next) { o->debug_connect = next; i++; }
        else if (!strcmp(a, "--search-roots") && next)  { o->search_roots = next; i++; }
        else if (!strcmp(a, "--build-index"))           { o->build_index = 1; }
        else if (!strcmp(a, "--search") && next)       { o->search_text = next; i++; }
        else if (!strcmp(a, "--assembly64-client-id") && next) { o->a64_id = next; i++; }
        else if (!strcmp(a, "--assembly64-url") && next)       { o->a64_url = next; i++; }
        else if (!strcmp(a, "--a64-search") && next)           { o->a64_search = next; i++; }
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
    if (!o->remote_host)
        o->remote_host = o->start_host;
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

#ifdef __PROSPERO__
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
    uint64_t underruns;
    uint64_t overflows;
} audio_out;

static int audio_open(audio_out *a, const options *o)
{
    memset(a, 0, sizeof(*a));
    SDL_AudioSpec want = {0}, have;
    want.freq = o->audio_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);   /* SDL resamples to the device */
    if (!a->dev) {
        fprintf(stderr, "audio: %s\n", SDL_GetError());
        return -1;
    }
    a->target = (Uint32)((Sint64)o->audio_rate * o->audio_latency_ms / 1000);
    a->prime = (Uint32)have.samples * 2;
    a->buffering = 1;
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
    SDL_QueueAudio(a->dev, samples, (Uint32)(frames * 4));
    if (a->buffering && queued + frames >= a->target + a->prime) {
        SDL_PauseAudioDevice(a->dev, 0);
        a->buffering = 0;
    }
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

#ifdef __PROSPERO__
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
#ifdef __PROSPERO__
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
    audio_init(&adec);
    if (opt.audio && audio_open(&aout, &opt) != 0) {
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
    uint64_t frames_at = 0;

    /* The first frames after the window opens can stall for ~0.5s, which
     * would arrive as an audio burst; discard audio until things settle. */
    Uint32 audio_from = SDL_GetTicks() + 1000;

    static ui_state ui;
    ui_init(&ui);

    /* Favourites and recents. */
    static fav_list favs, recents;
    fav_init(&favs, 0);
    fav_init(&recents, RECENTS_MAX);
    fav_load(&favs, favs_file);
    fav_load(&recents, recents_file);
    ui.favs = &favs;
    ui.recents = &recents;
    ui.a64_ready = opt.a64_id && *opt.a64_id;
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
    char browse_dir[512];
    snprintf(browse_dir, sizeof(browse_dir), "%s", opt.browse_dir ? opt.browse_dir : "/");

    /* Attach to the configured C64, or let the user pick one. */
    static session ses;
    discover *scan = NULL;
    int want_picker = opt.pick;
    char msg[160];
    if (opt.start_host) {
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
    notify(msg);
    if (want_picker) {
        ui_open_picker(&ui, opt.start_host ? msg : NULL);
        scan = discover_start(opt.start_host, opt.password);
    }
    if (opt.ui_screen && !strcmp(opt.ui_screen, "menu"))
        ui_open_menu(&ui);
    if (opt.ui_screen && !strcmp(opt.ui_screen, "assembly64"))
        ui_open_a64(&ui);
    if (opt.ui_screen && !strncmp(opt.ui_screen, "favourites", 10)) {
        ui_open_favs(&ui);
        if (!strcmp(opt.ui_screen, "favourites-games"))
            ui_input(&ui, IN_OK);
    }
    if (opt.ui_screen && !strcmp(opt.ui_screen, "recents"))
        ui_open_recents(&ui);
    if (opt.browse_path && ses.files) {
        ui_action a = ui_open_browser(&ui, opt.browse_path);
        files_list(ses.files, a.path);
    }
    ctl.remote = ses.rem;
    snprintf(ui.current_ip, sizeof(ui.current_ip), "%s", ses.host);
    int typed = 0;
    /* Assembly64: only with a client ID. */
    a64 *a64c = NULL;
    if (opt.a64_id && *opt.a64_id) {
        a64c = a64_start(opt.a64_url, opt.a64_id);
        if (a64c) {
            a64_load_categories(a64c);
            ui.a64_ready = 1;
        } else {
            fprintf(stderr, "assembly64: bad URL %s\n", opt.a64_url);
        }
    }
    if (opt.a64_search && a64c) {
        ui_open_a64(&ui);
        snprintf(ui.a64_query, sizeof(ui.a64_query), "%s", opt.a64_search);
        ui_action dbg = ui_input(&ui, IN_R2);
        ctl.actions[ctl.nactions++] = dbg;
    }

    /* Assembly64 item waiting for its file list before it starts. */
    char auto_item[64] = "";
    int auto_category = 0;
    fav_entry auto_entry;
    memset(&auto_entry, 0, sizeof(auto_entry));

    int index_was_building = 0;
    Uint32 search_refresh_at = 0, build_report_at = 0;
    if (opt.build_index && ses.index) {
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
            if (vic_feed(&dec, buf, (size_t)n))
                dirty = 1;
        }
        while (opt.audio) {
            ssize_t n = recv(asock, buf, sizeof(buf), 0);
            if (n < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                    perror("recv audio");
                break;
            }
            if (SDL_TICKS_PASSED(SDL_GetTicks(), audio_from) == 0)
                continue;
            size_t frames = audio_feed(&adec, buf, (size_t)n, samples, AUDIO_MAX_FRAMES);
            if (frames)
                audio_push(&aout, samples, frames);
        }

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
                snprintf(msg, sizeof(msg), "Connected to the C64 at %s", a.ip);
                notify(msg);
                controls_banner(&ctl, msg);
                break;
            }
            case UA_LISTEN_ONLY:
                discover_stop(scan);
                scan = NULL;
                session_end(&ses, &opt);
                if (config) {
                    static const char *const legacy[] = { "start", NULL };
                    config_set(config, "c64", "none", legacy);
                }
                controls_banner(&ctl, "Listening only - start the streams from the C64 menu");
                break;
            case UA_RESET:
            case UA_REBOOT:
                if (!ses.files) {
                    controls_banner(&ctl, "Choose a C64 first (Options > Choose C64)");
                    break;
                }
                files_action(ses.files, "", a.kind == UA_RESET ? FA_RESET : FA_REBOOT);
                controls_banner(&ctl, a.kind == UA_RESET ? "Resetting the C64..." : "Rebooting the C64...");
                break;
            case UA_A64_SET_ID:
                a64_stop(a64c);
                a64c = a64_start(opt.a64_url, a.text);
                ui.a64_ready = a64c != NULL;
                if (a64c)
                    a64_load_categories(a64c);
                if (config)
                    config_set(config, "assembly64-client-id", a.text, NULL);
                controls_banner(&ctl, a64c ? "Assembly64 client ID saved"
                                           : "Bad Assembly64 URL in the config");
                break;
            case UA_A64_SEARCH:
                if (a64c)
                    a64_search(a64c, a.text, a.category_key, a.offset);
                break;
            case UA_A64_ENTRIES:
                if (a64c)
                    a64_entries(a64c, a.text, a.category);
                break;
            case UA_A64_FILE:
                if (!ses.files) {
                    controls_banner(&ctl, "Choose a C64 first (Options > Choose C64)");
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
                    controls_banner(&ctl, "Choose a C64 first (Options > Choose C64)");
                    break;
                }
                ui.index = ses.index;
                ui_open_search(&ui);
                break;
            case UA_BUILD_INDEX:
                if (!ses.index) {
                    controls_banner(&ctl, "Choose a C64 first (Options > Choose C64)");
                    break;
                }
                index_build_start(ses.index, ses.host, ses.api.password, opt.search_roots, index_file);
                index_was_building = 1;
                ui.index = ses.index;
                ui_open_search(&ui);
                controls_banner(&ctl, "Building the search index...");
                break;
            case UA_OPEN_BROWSER:
                if (!ses.files) {
                    controls_banner(&ctl, "Choose a C64 first (Options > Choose C64)");
                    break;
                }
                a = ui_open_browser(&ui, browse_dir);
                files_list(ses.files, a.path);
                break;
            case UA_LIST:
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
                int local = e->kind == FAV_LOCAL_FILE || e->kind == FAV_LOCAL_FOLDER;
                if (!ses.files) {
                    controls_banner(&ctl, "Choose a C64 first (Options > Choose C64)");
                    break;
                }
                if (!local && !a64c) {
                    controls_banner(&ctl, "Assembly64 needs a client ID (Options > Search Assembly64)");
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
                           st.files, st.dirs, st.failed_dirs, (tick - started) / 1000.0, index_file);
                    running = 0;
                }
            }
        }

        if (a64c) {
            a64_result r;
            while (a64_poll(a64c, &r)) {
                if (r.kind == A64R_FILE) {
                    ui.a64_loading = 0;
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
                    if (ui.screen != UI_A64 && ui.screen != UI_A64_FILES && ui.screen != UI_A64_ID)
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
                    if (pick < 0) {
                        snprintf(msg, sizeof(msg), "Nothing to run in %s", auto_entry.name);
                        controls_banner(&ctl, msg);
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

        if (scan) {
            c64_device found[DISCOVER_MAX];
            int n, progress;
            int done = discover_poll(scan, found, &n, &progress);
            ui_set_devices(&ui, found, n, progress, done);
            if (done) {
                discover_stop(scan);
                scan = NULL;
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

        if (tex) {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);
        } else {
            draw_waiting(ren);
        }
        controls_draw(&ctl, ren, tick);
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
        if (!has_vsync)
            SDL_Delay(4);         /* don't spin; packets wait in the socket buffer */

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
            snprintf(title, sizeof(title),
                "C64 Ultimate - %dx%d  %.1f fps  lost %llu  bad %llu%s",
                dec.width, dec.height, fps,
                (unsigned long long)dec.stats.lost_packets,
                (unsigned long long)dec.stats.bad_packets, atitle);
            SDL_SetWindowTitle(win, title);
            stats_at = now;
            frames_at = dec.stats.frames;
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
    fav_free(&favs);
    fav_free(&recents);
    session_end(&ses, &opt);
    ui_free(&ui);
    if (opt.audio)
        SDL_CloseAudioDevice(aout.dev);
    if (asock >= 0)
        close(asock);
    if (tex) SDL_DestroyTexture(tex);
    font_cleanup();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    close(sock);
    return 0;
}
