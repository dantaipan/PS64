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

enum { UI_CLOSED, UI_MENU, UI_PICKER, UI_ADDRESS, UI_PASSWORD, UI_BROWSER, UI_ACTIONS,
       UI_SEARCH, UI_A64, UI_A64_FILES, UI_A64_ID, UI_FAVS, UI_FAV_LIST, UI_ABOUT };

/* Abstract inputs (controls.c maps buttons and keys to these). */
enum { IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_OK, IN_BACK, IN_ALT, IN_TRIANGLE,
       IN_L1, IN_R1, IN_L2, IN_R2 };

/* Menu items (ui_menu_items() lists the ones shown, in order). */
enum { MI_RESUME, MI_BROWSE, MI_SEARCH, MI_A64, MI_FAVS, MI_RECENTS, MI_RESET, MI_REBOOT,
       MI_AUDIO, MI_PORT, MI_CHOOSE, MI_INDEX, MI_QUIT,
       MI_DISK, MI_CONTROLLER,          /* emulator only */
       MI_ABOUT,
       MI_COUNT };

enum { UA_NONE, UA_MUTE, UA_PORT, UA_QUIT, UA_CONNECT, UA_LISTEN_ONLY, UA_RESCAN,
       UA_LIST, UA_FILE, UA_OPEN_BROWSER, UA_OPEN_SEARCH, UA_BUILD_INDEX, UA_RESET, UA_REBOOT,
       UA_A64_SEARCH, UA_A64_ENTRIES, UA_A64_FILE, UA_A64_SET_ID, UA_FAV_TOGGLE, UA_FAV_LAUNCH,
       UA_EMULATOR,      /* use the built-in emulator */
       UA_DISK,          /* emulator: insert the next (offset 1) or previous (-1) disk */
       UA_CONTROLLER,    /* emulator: D-pad as joystick <-> cursor keys */
       UA_A64_LATEST };  /* Assembly64: the latest releases (category_key) */

typedef struct {
    int  kind;
    char ip[16];
    char password[64];
    char path[512];
    int  file_action;
    /* Assembly64 requests. */
    char text[128];               /* query, client ID, or item ID */
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

typedef struct {
    int screen;
    int sel, top;

    /* Shown in the menu. */
    int muted, port;
    int emulator;                 /* the built-in emulator is the machine (set by main) */
    int disk, disks;              /* emulator: inserted disk (0-based) of a swappable set */
    int joy_keys;                 /* emulator: D-pad/stick/Cross are cursor keys and RETURN */
    char games_dir[256];          /* emulator: the real folder behind "/" (shown when empty) */

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
    int            a64_ready;       /* a client ID is configured (set by main) */
    char           a64_id[128];     /* client ID being entered */
    char           a64_query[64];
    int            a64_focus;       /* 0 keyboard, 1 results */
    int            a64_cat;         /* index into a64_cats, -1 = all */
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
void ui_open_a64_id(ui_state *u, const char *message);
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

/* Number of rows in the picker (devices + 4 fixed entries). */
int ui_picker_rows(const ui_state *u);
/* Parent folder of path ("/" for top level). */
void ui_parent(const char *path, char *out, size_t cap);

#endif
