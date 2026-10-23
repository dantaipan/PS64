/*
 * ui - the in-app screens: Options menu, C64 picker, address and password
 * entry, file browser. Pure state machine; drawing is in ui_sdl.c and the
 * work (network, config) is done by main on the actions returned here.
 */
#ifndef UI_H
#define UI_H

#include "a64.h"
#include "c64files.h"
#include "c64index.h"
#include "discover.h"
#include "favs.h"
#include "osk.h"

#include <stdint.h>

enum { UI_CLOSED, UI_MENU, UI_PICKER, UI_ADDRESS, UI_PASSWORD, UI_BROWSER, UI_ACTIONS,
       UI_SEARCH, UI_A64, UI_A64_FILES, UI_FAVS, UI_FAV_LIST, UI_ABOUT,
       UI_HOME,     /* home screen: emulator, C64 Ultimate stream, SID player (drawn by home_sdl.c) */
       UI_SID };    /* the SID player */

/* Abstract inputs (controls.c maps buttons and keys to these). */
enum { IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_OK, IN_BACK, IN_ALT, IN_TRIANGLE,
       IN_L1, IN_R1, IN_L2, IN_R2 };

/* Menu items (ui_menu_items() lists the ones shown, in order). */
enum { MI_HOME, MI_RESUME, MI_BROWSE, MI_SEARCH, MI_A64, MI_FAVS, MI_RECENTS, MI_RESET, MI_REBOOT,
       MI_AUDIO, MI_PORT, MI_INDEX, MI_QUIT,
       MI_DISK, MI_CONTROLLER,          /* emulator only */
       MI_ABOUT,
       MI_CART,                         /* the cartridge kept in the port (e.g. a fast loader) */
       MI_COUNT };

enum { UA_NONE, UA_MUTE, UA_PORT, UA_QUIT, UA_CONNECT, UA_RESCAN,
       UA_LIST, UA_FILE, UA_OPEN_BROWSER, UA_OPEN_SEARCH, UA_BUILD_INDEX, UA_RESET, UA_REBOOT,
       UA_A64_SEARCH, UA_A64_ENTRIES, UA_A64_FILE, UA_FAV_TOGGLE, UA_FAV_LAUNCH,
       UA_HOME,          /* menu: back to the home screen */
       UA_HOME_VICE,     /* home: the built-in emulator (resumes it if it ran) */
       UA_HOME_STREAM,   /* home: the C64 Ultimate's stream (it's live) */
       UA_HOME_SID,      /* home: the SID player (path: its folder to list) */
       UA_SID_LIST,      /* SID player: list folder path of tab category (Local, C64 Ultimate) */
       UA_SID_PLAY,      /* SID player: play entry offset of tab category (SID_TAB_*) */
       UA_SID_SONG,      /* SID player: subtune -1 / +1 (offset) */
       UA_SID_SKIP,      /* SID player: previous (-1) or next (1) tune */
       UA_SID_PAUSE,     /* SID player: pause / carry on */
       UA_SID_MODE,      /* SID player: in order -> shuffle -> repeat one */
       UA_SID_OUTPUT,    /* SID player: play on PS64 <-> on the C64 Ultimate */
       UA_SID_STOP,      /* SID player: stop (leaving it) */
       UA_DISK,          /* emulator: insert the next (offset 1) or previous (-1) disk */
       UA_CONTROLLER,    /* emulator: D-pad as joystick <-> cursor keys */
       UA_A64_LATEST,    /* Assembly64: the latest releases (category_key) */
       UA_VOLUME,        /* audio: one step quieter (offset -1) or louder (1) */
       UA_CART,          /* menu: put cartridge text in (a name from carts[]; "" takes it out) */
       UA_HOME_MUSIC,    /* the home screen's music: level offset -1/+1, or 0 off <-> on */
       UA_HOME_NEXT };   /* home: another random tune */

typedef struct {
    int  kind;
    char ip[16];
    char password[64];
    char path[512];
    int  file_action;
    /* Assembly64 requests. */
    char text[128];               /* query, or item ID */
    char category_key[40];
    int  category, file_id, offset;
    /* Favourites: the entry to toggle or launch (fav.action = action to use). */
    fav_entry fav;
} ui_action;

#define UI_PAGE 20          /* rows per page in the picker and browser */
#define UI_SEARCH_PAGE 10   /* result rows shown above the on-screen keyboard */
#define UI_RESULTS_PAGE 20  /* result rows when the results have focus (no keyboard) */
#define UI_SEARCH_MAX 300   /* results kept per query */
#define UI_ABOUT_ROWS 22    /* About text lines shown at once */

enum { SHOT_NONE, SHOT_LOADING, SHOT_READY };

/* The home screen's music: levels 1..10 (0 is off), UI_HOME_MUSIC_STEP dB apart below 0 dB. */
#define UI_HOME_MUSIC_MAX     10
#define UI_HOME_MUSIC_DEFAULT 5
#define UI_HOME_MUSIC_STEP    4
int ui_home_music_db(int level);

/* Audio level in the menu: 0 dB down to UI_VOLUME_MIN in UI_VOLUME_STEP steps, then Off. */
#define UI_VOLUME_MIN  (-30)
#define UI_VOLUME_STEP 2

/* One step of the Audio item (dir -1 quieter, 1 louder): below the lowest level is Off,
 * louder from Off comes back at the lowest level. Updates *db and *muted. */
void ui_volume_step(int *db, int *muted, int dir);

/* Cartridges offered in the menu (the emulator's carts folder, the C64 Ultimate's
 * /flash/carts): a list of names, the one in, and whether the list is known yet. */
#define UI_CARTS 16
enum { CARTS_LOADING, CARTS_READY, CARTS_ERROR };

/* SID player: its lists (the C64 Ultimate's while one is connected), and the size of the
 * oscilloscope's sample window. */
enum { SID_TAB_LOCAL, SID_TAB_A64, SID_TAB_ULTIMATE, SID_TABS };

/* A folder list of the SID player (Local, the C64 Ultimate's storage). */
typedef struct {
    char       path[512];         /* the folder shown ("/" = its top) */
    ftp_entry *entries;           /* owned: its folders and .sid files */
    int        count, loading;
    char       error[128];
} sid_folder;
#define SID_SCOPE 384

/* Home screen items, focus and the C64 Ultimate stream's state. */
enum { HOME_VICE, HOME_STREAM, HOME_SID, HOME_ITEMS };
enum { HOME_ON_ITEMS, HOME_ON_POWER, HOME_ON_GEAR };
enum { HOME_STREAM_SEARCHING,     /* looking for the C64 Ultimate */
       HOME_STREAM_CONNECTING,    /* found: starting its stream */
       HOME_STREAM_LIVE,          /* pictures arriving */
       HOME_STREAM_LOCKED,        /* found, but it needs a password (settings) */
       HOME_STREAM_NONE };        /* not found */

typedef struct {
    int screen;
    int sel, top;

    /* Shown in the menu. */
    int muted, port;
    int volume_db;                /* level of the source playing (UI_VOLUME_MIN..0) */
    int emulator;                 /* the built-in emulator is the machine (set by main) */
    int disk, disks;              /* emulator: inserted disk (0-based) of a swappable set */
    int joy_keys;                 /* emulator: D-pad/stick/Cross are cursor keys and RETURN */
    char games_dir[256];          /* emulator: the real folder behind "/" (shown when empty) */
    char carts[UI_CARTS][64];     /* cartridge files (set by main) */
    int  ncarts, cart_sel;        /* cart_sel: index of the one in, -1 none */
    int  carts_state;             /* CARTS_* */
    int  home_music;              /* the home screen's music level, 0 (off)..UI_HOME_MUSIC_MAX (set by main) */
    char home_tune[128];          /* what it plays ("" nothing; set by main) */

    /* Home screen. */
    int  home_sel;                /* HOME_VICE, HOME_STREAM, HOME_SID */
    int  home_last;               /* the object chosen last (-1 none): another clears the search */
    int  home_focus;              /* HOME_ON_ITEMS, or one of the icons at the top */
    char home_snack[64];          /* snackbar text; home_snack_serial rises for each new one */
    unsigned home_snack_serial;
    int  home_stream;             /* HOME_STREAM_* (set by main) */
    char home_ip[16];             /* the C64 Ultimate found (set by main) */
    int  picker_from_home;        /* the picker is the home screen's settings */

    /* SID player. Lists: */
    int        sid_tab;           /* SID_TAB_* */
    sid_folder sid_dirs[SID_TABS];  /* [SID_TAB_LOCAL], [SID_TAB_ULTIMATE] ([SID_TAB_A64] unused) */
    a64_item  *sid_a64;           /* owned: the Assembly64 music results played from */
    int        sid_a64_count;
    int        sid_sel[SID_TABS], sid_top[SID_TABS];
    int        a64_for_sid;       /* the Assembly64 search was opened by the SID player */
    int        a64_results_sid;   /* the search's query and results are the SID player's */
    int        a64_pending_sid;   /* the search on its way was sent by the SID player */
    /* Now playing (set by main). */
    int        sid_active, sid_paused, sid_mode, sid_on_ultimate, sid_can_ultimate;
    int        sid_prefer_ultimate;   /* "Play on: C64 Ultimate" chosen (the next tune plays there) */
    int        sid_play_tab, sid_play_index;   /* the list entry playing, -1 none */
    char       sid_title[64], sid_author[64], sid_released[64], sid_status[96];
    int        sid_song, sid_songs, sid_model, sid_ntsc;
    unsigned   sid_time_ms;
    int        sid_length_ms;     /* -1: unknown (plays SID_DEFAULT_MS) */
    unsigned char sid_regs[32];   /* the SID's registers (voice bars) */
    short      sid_scope[SID_SCOPE];  /* the latest sound, mono (oscilloscope) */

    /* Picker. */
    c64_device devices[DISCOVER_MAX];
    int        ndevices, progress, scanning;
    char       message[128];
    char       current_ip[16];

    /* Address entry. */
    int octets[4], field;

    /* Password entry (on-screen keyboard used locally). */
    osk_state kbd;
    char      password[64];
    char      pending_ip[16];

    /* File browser. */
    char       path[512];
    ftp_entry *entries;           /* owned */
    int        count, loading;
    char       error[160];
    /* Action list (for a browser entry or a search result). */
    int        actions[FA_COUNT], nactions, action_sel;
    char       action_path[512];
    int        action_return;     /* screen to go back to */

    /* Search. */
    c64index  *index;             /* set by main; NULL = no C64 */
    char       query[64];
    int        search_focus;      /* 0 keyboard, 1 results */
    index_hit  results[UI_SEARCH_MAX];
    int        nresults, rsel, rtop;

    /* Assembly64. */
    int            a64_ready;       /* the Assembly64 client is running (set by main) */
    char           a64_query[64];
    int            a64_focus;       /* 0 keyboard, 1 results */
    int            a64_cat;         /* index into a64_cats, -1 = all (Games until one is chosen) */
    int            a64_cat_chosen;  /* L2 has chosen a category (else Games once the list arrives) */
    a64_categories a64_cats;
    a64_item      *a64_items;       /* owned */
    int            a64_count, a64_more, a64_loading, a64_sel, a64_top;
    char           a64_status[160];
    a64_item       a64_cur;         /* item whose files are shown */
    a64_file      *a64_files;       /* owned */
    int            a64_nfiles, a64_fsel, a64_ftop;
    /* Result category numbers seen in filtered searches -> filter name
     * (the server's category list doesn't carry the numbers). */
    int            a64_seen_num[A64_MAX_CATS];
    char           a64_seen_name[A64_MAX_CATS][40];
    int            a64_nseen;
    char           a64_search_cat[40];   /* filter name used by the pending search */
    int            a64_latest;      /* the results are the latest releases (dates shown) */
    /* Screenshot of the highlighted result (CSDb items; pixels owned by main). */
    int             a64_shot_state;  /* SHOT_NONE / SHOT_LOADING / SHOT_READY */
    const uint32_t *a64_shot;        /* ARGB8888 frames, a64_shot_w x a64_shot_h each, stacked */
    int             a64_shot_w, a64_shot_h;
    int             a64_shot_frames; /* several for an animated GIF: shown in turn */
    const int      *a64_shot_delays; /* ms per frame */
    unsigned        a64_shot_serial; /* raised for each new picture (the UI re-uploads it) */

    /* Favourites and recents (lists owned by main). */
    fav_list      *favs, *recents;
    int            fav_recents;     /* UI_FAV_LIST shows recents (else favourites) */
    int            fav_group;       /* FAV_GAMES / FAV_SIDS */
    int            fl_sel, fl_top;
    fav_entry      action_fav;      /* entry the action list applies to */

    /* About. */
    int            about_top;       /* first text line shown */
} ui_state;

void ui_setup(ui_state *u);
void ui_free(ui_state *u);

void ui_open_menu(ui_state *u);
void ui_open_picker(ui_state *u, const char *message);
/* The home screen (keeps the item chosen last), and its settings (the gear, or Options):
 * the C64 Ultimate to stream from and the home music; returns the search to start. */
void ui_open_home(ui_state *u);
ui_action ui_open_home_settings(ui_state *u);

/* The SID player (returns the UA_SID_LIST for its folder), a folder listing of a tab
 * (takes entries; keeps folders and .sid files) and a failed listing. */
ui_action ui_open_sid(ui_state *u);
void ui_sid_set_listing(ui_state *u, int tab, const char *path, ftp_entry *entries, int n);
void ui_sid_list_error(ui_state *u, int tab, const char *path, const char *message);
/* Whether a C64 Ultimate is connected (the "Play on" row): keeps the selection on the
 * same entry when that row comes or goes. */
void ui_sid_set_ultimate(ui_state *u, int connected);
#define SID_DEFAULT_MS 180000     /* tunes without a known length play this long */
/* Opens the browser at path; returns a UA_LIST action to fetch it. */
ui_action ui_open_browser(ui_state *u, const char *path);
void ui_close(ui_state *u);

/* Feed scan results / a finished listing (takes ownership of entries). */
void ui_set_devices(ui_state *u, const c64_device *d, int n, int progress, int done);
void ui_set_listing(ui_state *u, const char *path, ftp_entry *entries, int count);
void ui_set_list_error(ui_state *u, const char *path, const char *message);

void ui_open_search(ui_state *u);

/* Assembly64 screens; results from the a64 worker are fed in here. */
void ui_open_a64(ui_state *u);
void ui_a64_results(ui_state *u, a64_result *r);   /* takes ownership of r's lists */
void ui_a64_error(ui_state *u, const char *message, int auth_error);

/* About: what PS64 does, the controls, thanks. */
void ui_open_about(ui_state *u);
const char *const *ui_about_text(int *count);

void ui_open_favs(ui_state *u);
void ui_open_recents(ui_state *u);
/* Keep the favourites/recents selection valid after the lists changed. */
void ui_favs_changed(ui_state *u);
/* Whether a title is a favourite (for the star marker). */
int  ui_is_fav_local(const ui_state *u, const char *path, int is_dir);
int  ui_is_fav_a64(const ui_state *u, const a64_item *item, const a64_file *file);
/* Rows in UI_FAV_LIST and the entry for a row. */
int  ui_fav_rows(const ui_state *u);
const fav_entry *ui_fav_row(const ui_state *u, int row);
/* Re-run the current query (e.g. while the index is still being built). */
void ui_search_refresh(ui_state *u);
/* Typed text from a real keyboard (search query). */
void ui_text(ui_state *u, const char *text);

ui_action ui_input(ui_state *u, int input);

/* Result rows the search screens show: fewer while the on-screen keyboard is up
 * (focus 0), the whole panel when the results have focus. */
int ui_search_rows(int focus);

/* Menu items shown for the current machine; returns the count. */
int ui_menu_items(const ui_state *u, int *out);

/* Number of rows in the picker (devices + Enter address, Search again, Home music). */
int ui_picker_rows(const ui_state *u);
/* Parent folder of path ("/" for top level). */
void ui_parent(const char *path, char *out, size_t cap);

#endif
