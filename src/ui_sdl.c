#include "ui_sdl.h"

#include "font_sdl.h"
#include "osk_sdl.h"

#include <stdio.h>
#include <string.h>

#define PX     56                       /* panel */
#define PY     28
#define PW     (CANVAS_W - 2 * PX)
#define PH     (CANVAS_H - 2 * PY)
#define ROW_H  18
#define LIST_Y (PY + 52)
#define CHARS  ((PW - 32) / 8)          /* characters per row */
#define STAR   '\x85'                   /* favourite marker (font glyph) */

static const SDL_Color WHITE = { 255, 255, 255, 255 }, DIM = { 150, 150, 160, 255 },
                       YELLOW = { 0xED, 0xF1, 0x71, 255 }, RED = { 0xFF, 0x80, 0x80, 255 },
                       CYAN = { 0x75, 0xCE, 0xC8, 255 };

static void panel(SDL_Renderer *ren, const char *title)
{
    fill_rect(ren, PX, PY, PW, PH, 0x10, 0x10, 0x20, 240);
    fill_rect(ren, PX, PY, PW, 30, 0x2E, 0x2C, 0x9B, 255);           /* C64 blue title bar */
    font_draw(ren, title, PX + 16, PY + 11, 1, WHITE);
}

static void footer(SDL_Renderer *ren, const char *text)
{
    font_draw(ren, text, PX + 16, PY + PH - 18, 1, DIM);
}

/* Copy s into out, cutting it to at most width characters with "..". */
static void fit(const char *s, int width, char *out, size_t cap)
{
    int n = (int)strlen(s);
    if (n <= width)
        snprintf(out, cap, "%s", s);
    else
        snprintf(out, cap, "%.*s..", width > 2 ? width - 2 : 0, s);
}

/* Keep the end of a path visible: "../games/A/Ace of Aces". */
static void fit_left(const char *s, int width, char *out, size_t cap)
{
    int n = (int)strlen(s);
    if (n <= width)
        snprintf(out, cap, "%s", s);
    else
        snprintf(out, cap, "..%s", s + n - (width - 2));
}

static void row(SDL_Renderer *ren, int i, int selected, const char *text, SDL_Color c)
{
    int y = LIST_Y + i * ROW_H;
    if (selected)
        fill_rect(ren, PX + 8, y - 5, PW - 16, ROW_H, 0x70, 0x6D, 0xEB, 255);
    font_draw(ren, text, PX + 16, y, 1, selected ? WHITE : c);
}

static void draw_menu(SDL_Renderer *ren, const ui_state *u)
{
    panel(ren, "PS64");
    char port[32], disk[40];
    snprintf(port, sizeof(port), "Joystick port: < %d >", u->port);
    snprintf(disk, sizeof(disk), "Disk: < %d of %d >", u->disk + 1, u->disks);
    const char *labels[MI_COUNT] = {
        [MI_RESUME] = "Resume",
        [MI_BROWSE] = u->emulator ? "Browse games folder..." : "Browse files...",
        [MI_SEARCH] = u->emulator ? "Search games folder..." : "Search files...",
        [MI_A64] = "Search Assembly64...",
        [MI_FAVS] = "Favourites...", [MI_RECENTS] = "Recents...",
        [MI_RESET] = "Reset C64",
        [MI_REBOOT] = u->emulator ? "Switch off and on (removes disks)" : "Reboot C64 (clears cartridge)",
        [MI_AUDIO] = u->muted ? "Audio: < Off >" : "Audio: < On >",
        [MI_PORT] = port,
        [MI_CHOOSE] = "Choose C64 stream or emulator...",
        [MI_INDEX] = "Update search index", [MI_ABOUT] = "About PS64...", [MI_QUIT] = "Quit",
        [MI_DISK] = disk,
        [MI_CONTROLLER] = u->joy_keys ? "D-pad: < Cursor keys >" : "D-pad: < Joystick >",
    };
    int items[MI_COUNT];
    int n = ui_menu_items(u, items);
    for (int i = 0; i < n; i++)
        row(ren, i, i == u->sel, labels[items[i]], WHITE);
    if (u->emulator || u->current_ip[0]) {
        char line[64];
        snprintf(line, sizeof(line), u->emulator ? "Built-in emulator (VICE)%.0s" : "C64: %s",
                 u->current_ip);
        font_draw(ren, line, PX + 16, LIST_Y + (n + 1) * ROW_H, 1, DIM);
    }
    footer(ren, "X select   O close   </> change");
}

static void draw_picker(SDL_Renderer *ren, const ui_state *u, Uint32 now)
{
    panel(ren, "Choose C64 stream or emulator");
    char status[96];
    if (u->scanning)
        snprintf(status, sizeof(status), "Searching the network %c %d%%",
                 "|/-\\"[(now / 150) % 4], u->progress);
    else
        snprintf(status, sizeof(status), u->ndevices ? "Found %d:" : "No C64 Ultimate found%.0d",
                 u->ndevices);
    font_draw(ren, u->message[0] ? u->message : status, PX + 16, PY + 38, 1,
              u->message[0] ? YELLOW : DIM);

    int rows = ui_picker_rows(u);
    for (int i = u->top; i < rows && i < u->top + UI_PAGE; i++) {
        char text[160], line[128];
        SDL_Color c = WHITE;
        if (i < u->ndevices) {
            const c64_device *d = &u->devices[i];
            if (d->locked)
                snprintf(text, sizeof(text), "%-15s  (password protected)", d->ip);
            else
                snprintf(text, sizeof(text), "%-15s  %s  %s %s", d->ip,
                         d->hostname[0] ? d->hostname : "-", d->product, d->firmware);
        } else {
            static const char *const fixed[4] = {
                "Enter address...", "Search again",
                "Built-in emulator (no C64 Ultimate needed)",
                "No C64 - listen only (start streams from its menu)",
            };
            snprintf(text, sizeof(text), "%s", fixed[i - u->ndevices]);
            c = CYAN;
        }
        fit(text, CHARS, line, sizeof(line));
        row(ren, i - u->top, i == u->sel, line, c);
    }
    footer(ren, "X choose   O close      The C64 needs its web remote control turned on");
}

static void draw_about(SDL_Renderer *ren, const ui_state *u)
{
    panel(ren, "About PS64");
    int count;
    const char *const *text = ui_about_text(&count);
    for (int i = 0; i < UI_ABOUT_ROWS && u->about_top + i < count; i++) {
        const char *line = text[u->about_top + i];
        /* Section headings in yellow. */
        int heading = line[0] && line[0] != ' ' && line[strlen(line) - 1] != '.' &&
                      line[strlen(line) - 1] != ',' && line[strlen(line) - 1] != ':' &&
                      strchr(line, ' ') == NULL;
        font_draw(ren, line, PX + 16, LIST_Y + i * ROW_H, 1, heading ? YELLOW : WHITE);
    }
    footer(ren, count > UI_ABOUT_ROWS ? "Up/Down scroll   L1/R1 page   O back" : "O back");
}

static void draw_address(SDL_Renderer *ren, const ui_state *u)
{
    panel(ren, "C64 address");
    int x = PX + (PW - 4 * 3 * 8 * 3 - 3 * 24) / 2, y = PY + 140;
    for (int i = 0; i < 4; i++) {
        char num[8];
        snprintf(num, sizeof(num), "%3d", u->octets[i]);
        int w = 3 * 8 * 3;
        if (i == u->field)
            fill_rect(ren, x - 6, y - 8, w + 12, 24 + 16, 0x70, 0x6D, 0xEB, 255);
        font_draw(ren, num, x, y, 3, WHITE);
        x += w;
        if (i < 3) {
            font_draw(ren, ".", x + 4, y, 3, DIM);
            x += 24;
        }
    }
    footer(ren, "</> field   Up/Down +1/-1   L1/R1 -10/+10   X connect   O back");
}

static void draw_password(SDL_Renderer *ren, const ui_state *u)
{
    char title[64], masked[64];
    snprintf(title, sizeof(title), "Network password for %s", u->pending_ip);
    panel(ren, title);
    size_t n = strlen(u->password);
    memset(masked, '*', n);
    masked[n] = 0;
    fill_rect(ren, PX + 16, PY + 60, PW - 32, 28, 0x30, 0x30, 0x40, 255);
    font_draw(ren, masked, PX + 24, PY + 70, 1, WHITE);
    font_draw(ren, "X type   [] delete   R2 / RETURN connect   L1 shift   O back",
              PX + 16, PY + 100, 1, DIM);
    osk_draw(ren, &u->kbd);
}

static void draw_browser(SDL_Renderer *ren, const ui_state *u)
{
    char title[128];
    fit_left(u->path, CHARS, title, sizeof(title));
    panel(ren, title);

    if (u->loading) {
        font_draw(ren, "Loading...", PX + 16, LIST_Y, 1, DIM);
    } else if (u->error[0]) {
        font_draw(ren, u->error, PX + 16, LIST_Y, 1, RED);
    } else if (u->count == 0 && u->emulator && !strcmp(u->path, "/")) {
        char line[300];
        font_draw(ren, "The games folder is empty. Copy games (D64, PRG, T64, TAP, CRT) into",
                  PX + 16, LIST_Y, 1, WHITE);
        snprintf(line, sizeof(line), "%s", u->games_dir);
        font_draw(ren, line, PX + 16, LIST_Y + ROW_H, 1, CYAN);
        font_draw(ren, "over FTP, or find games with Search Assembly64.", PX + 16, LIST_Y + 2 * ROW_H, 1, WHITE);
    } else if (u->count == 0) {
        font_draw(ren, "(empty folder)", PX + 16, LIST_Y, 1, DIM);
    }

    for (int i = u->top; !u->loading && i < u->count && i < u->top + UI_PAGE; i++) {
        const ftp_entry *e = &u->entries[i];
        int type = files_type(e->name, e->is_dir);
        char name[128], size[16] = "", line[160];
        fit(e->name, CHARS - 16, name, sizeof(name));
        if (!e->is_dir) {
            if (e->size >= 1024 * 1024)
                snprintf(size, sizeof(size), "%lluM", e->size / (1024 * 1024));
            else
                snprintf(size, sizeof(size), "%lluK", (e->size + 1023) / 1024);
        }
        char full[700];
        snprintf(full, sizeof(full), "%s%s%s", u->path, strcmp(u->path, "/") ? "/" : "", e->name);
        snprintf(line, sizeof(line), "%c%-4s %-*s %7s", ui_is_fav_local(u, full, e->is_dir) ? STAR : ' ',
                 files_type_tag(type), CHARS - 16, name, size);
        row(ren, i - u->top, i == u->sel, line,
            type == FT_DIR ? CYAN : type == FT_OTHER ? DIM : WHITE);
    }

    if (!u->loading && u->sel < u->count && strlen(u->entries[u->sel].name) > (size_t)(CHARS - 16)) {
        char full[128];
        fit(u->entries[u->sel].name, CHARS, full, sizeof(full));
        font_draw(ren, full, PX + 16, PY + PH - 36, 1, YELLOW);
    }
    footer(ren, "X open/run  [] more  /\\ favourite  L2 search  O up  L1/R1 page");
}

static void draw_search(SDL_Renderer *ren, const ui_state *u, Uint32 now);
static void draw_a64_files(SDL_Renderer *ren, const ui_state *u, Uint32 now);
static void draw_fav_list(SDL_Renderer *ren, const ui_state *u);

static void draw_actions(SDL_Renderer *ren, const ui_state *u, Uint32 now)
{
    if (u->action_return == UI_SEARCH)
        draw_search(ren, u, now);
    else if (u->action_return == UI_A64_FILES)
        draw_a64_files(ren, u, now);
    else if (u->action_return == UI_FAV_LIST)
        draw_fav_list(ren, u);
    else
        draw_browser(ren, u);
    int w = 300, h = 40 + u->nactions * ROW_H, x = (CANVAS_W - w) / 2, y = (CANVAS_H - h) / 2 - 60;
    fill_rect(ren, x, y, w, h, 0x20, 0x20, 0x30, 250);
    fill_rect(ren, x, y, w, 24, 0x2E, 0x2C, 0x9B, 255);
    const char *base = strrchr(u->action_path, '/');
    char name[48];
    fit(base ? base + 1 : u->action_path, (w - 24) / 8, name, sizeof(name));
    font_draw(ren, name, x + 12, y + 8, 1, WHITE);
    for (int i = 0; i < u->nactions; i++) {
        int ry = y + 32 + i * ROW_H;
        if (i == u->action_sel)
            fill_rect(ren, x + 6, ry - 5, w - 12, ROW_H, 0x70, 0x6D, 0xEB, 255);
        font_draw(ren, files_action_name(u->actions[i]), x + 14, ry, 1, WHITE);
    }
}

static void draw_search(SDL_Renderer *ren, const ui_state *u, Uint32 now)
{
    panel(ren, u->emulator ? "Search the games folder" : "Search the C64's files");
    index_status st;
    memset(&st, 0, sizeof(st));
    int count = 0;
    if (u->index) {
        index_status_get(u->index, &st);
        count = index_count(u->index);
    }

    if (!u->index) {
        font_draw(ren, "Choose a C64 first (Options > Choose C64...)", PX + 16, LIST_Y, 1, YELLOW);
        footer(ren, "O close");
        return;
    }
    if (count == 0 && !st.building) {
        font_draw(ren, st.error[0] ? st.error : u->emulator ? "There's no search index for the games folder yet."
                                                            : "There's no search index for this C64 yet.",
                  PX + 16, LIST_Y, 1, st.error[0] ? RED : WHITE);
        font_draw(ren, u->emulator ? "Building one lists every folder in it."
                                   : "Building one lists every folder on its storage over FTP.",
                  PX + 16, LIST_Y + 2 * ROW_H, 1, DIM);
        font_draw(ren, "It can take a few minutes; you can search while it runs.",
                  PX + 16, LIST_Y + 3 * ROW_H, 1, DIM);
        footer(ren, "X build the index   O close");
        return;
    }

    /* Query field with a blinking cursor. */
    char field[80];
    snprintf(field, sizeof(field), "%s%s", u->query, u->search_focus == 0 && (now / 400) % 2 ? "_" : "");
    fill_rect(ren, PX + 16, PY + 38, PW - 32, 22, 0x30, 0x30, 0x40, 255);
    font_draw(ren, field, PX + 24, PY + 45, 1, WHITE);

    /* Status line: index state and result count. */
    char status[160];
    if (st.building)
        snprintf(status, sizeof(status), "Indexing %c %d folders, %d files - %s",
                 "|/-\\"[(now / 150) % 4], st.dirs, st.files, st.current);
    else if (u->query[0])
        snprintf(status, sizeof(status), "%d match%s%s  (%d files indexed)", u->nresults,
                 u->nresults == 1 ? "" : "es", u->nresults == UI_SEARCH_MAX ? " or more" : "", count);
    else
        snprintf(status, sizeof(status), "Type part of a name. %d files indexed.", count);
    char line[128];
    fit(status, CHARS, line, sizeof(line));
    font_draw(ren, line, PX + 16, PY + 68, 1, st.building ? YELLOW : DIM);

    /* Results. */
    const int ry = PY + 88;
    for (int i = u->rtop; i < u->nresults && i < u->rtop + ui_search_rows(u->search_focus); i++) {
        /* Show "<folder>/<file>" (games are often Title/Disk1.d64) and the
         * rest of the path, shortened from the left. */
        const index_hit *h = &u->results[i];
        const char *title = h->path, *slash = strrchr(h->path, '/');
        for (const char *p = slash ? slash - 1 : NULL; p && p >= h->path; p--)
            if (*p == '/') {
                title = p + 1;
                break;
            }
        char name[96], folder[96], text[200], dir[256];
        fit(title, 40, name, sizeof(name));
        snprintf(dir, sizeof(dir), "%.*s", title > h->path ? (int)(title - h->path - 1) : 0, h->path);
        fit_left(dir, CHARS - 47, folder, sizeof(folder));
        snprintf(text, sizeof(text), "%c%-4s %-40s %s", ui_is_fav_local(u, h->path, 0) ? STAR : ' ',
                 files_type_tag(h->type), name, folder);
        int y = ry + (i - u->rtop) * ROW_H;
        int selected = u->search_focus == 1 && i == u->rsel;
        if (selected)
            fill_rect(ren, PX + 8, y - 5, PW - 16, ROW_H, 0x70, 0x6D, 0xEB, 255);
        font_draw(ren, text, PX + 16, y, 1, selected ? WHITE : (i == u->rsel ? CYAN : WHITE));
    }

    if (u->search_focus == 0) {
        font_draw(ren, "X type  [] del  /\\ space  R2 results  L1 shift  O close",
                  PX + 16, ry + UI_SEARCH_PAGE * ROW_H + 4, 1, DIM);
        osk_draw(ren, &u->kbd);
    } else {
        footer(ren, "X run  [] more  /\\ favourite  > show folder  O back to typing");
    }
}

/* ---- Assembly64 ---------------------------------------------------------------- */

static void text_field(SDL_Renderer *ren, int y, const char *text, int cursor, Uint32 now)
{
    char field[160];
    snprintf(field, sizeof(field), "%s%s", text, cursor && (now / 400) % 2 ? "_" : "");
    fill_rect(ren, PX + 16, y, PW - 32, 22, 0x30, 0x30, 0x40, 255);
    font_draw(ren, field, PX + 24, y + 7, 1, WHITE);
}

static void draw_a64_id(SDL_Renderer *ren, const ui_state *u, Uint32 now)
{
    panel(ren, "Assembly64 client ID");
    font_draw(ren, "Searching Assembly64 needs a client ID from its team",
              PX + 16, PY + 38, 1, DIM);
    font_draw(ren, "(hackerswithstyle.se). Type it below; it's saved in ps64.cfg.",
              PX + 16, PY + 52, 1, DIM);
    text_field(ren, PY + 70, u->a64_id, 1, now);
    if (u->a64_status[0]) {
        char line[128];
        fit(u->a64_status, CHARS, line, sizeof(line));
        font_draw(ren, line, PX + 16, PY + 100, 1, RED);
    }
    font_draw(ren, "X type  [] del  R2 / RETURN save  L1 shift  O close",
              PX + 16, PY + 120, 1, DIM);
    osk_draw(ren, &u->kbd);
}

static void draw_a64(SDL_Renderer *ren, const ui_state *u, Uint32 now)
{
    panel(ren, "Search Assembly64");
    text_field(ren, PY + 38, u->a64_query, u->a64_focus == 0, now);

    char status[200], cat[64];
    snprintf(cat, sizeof(cat), "%s", u->a64_cat >= 0 && u->a64_cat < u->a64_cats.count
             ? u->a64_cats.names[u->a64_cat] : "All");
    snprintf(status, sizeof(status), "Category: < %s >   %s", cat, u->a64_status);
    if (u->a64_loading)
        snprintf(status, sizeof(status), "Category: < %s >   %c %s", cat,
                 "|/-\\"[(now / 150) % 4], u->a64_status);
    char line[128];
    fit(status, CHARS, line, sizeof(line));
    font_draw(ren, line, PX + 16, PY + 68, 1, u->a64_loading ? YELLOW : DIM);

    const int ry = PY + 88;
    int rows = u->a64_count + (u->a64_more ? 1 : 0);
    for (int i = u->a64_top; i < rows && i < u->a64_top + ui_search_rows(u->a64_focus); i++) {
        char text[200], name[64], group[40];
        if (i < u->a64_count) {
            const a64_item *it = &u->a64_items[i];
            char when[12] = "";
            int star = ui_is_fav_a64(u, it, NULL) ? STAR : ' ';
            if (u->a64_latest) {
                /* The latest releases: the full release date, so the order shows. */
                fit(it->name, 40, name, sizeof(name));
                fit(it->group, 20, group, sizeof(group));
                snprintf(when, sizeof(when), "%s", it->released[0] ? it->released : "");
                snprintf(text, sizeof(text), "%c%-40s %-20s %10s", star, name, group, when);
            } else {
                fit(it->name, 44, name, sizeof(name));
                fit(it->group, 22, group, sizeof(group));
                if (it->year > 0)
                    snprintf(when, sizeof(when), "%d", it->year);
                snprintf(text, sizeof(text), "%c%-44s %-22s %4s", star, name, group, when);
            }
        } else {
            snprintf(text, sizeof(text), "More results...");
        }
        int y = ry + (i - u->a64_top) * ROW_H;
        int selected = u->a64_focus == 1 && i == u->a64_sel;
        if (selected)
            fill_rect(ren, PX + 8, y - 5, PW - 16, ROW_H, 0x70, 0x6D, 0xEB, 255);
        font_draw(ren, text, PX + 16, y, 1, i < u->a64_count ? WHITE : CYAN);
    }

    if (u->a64_focus == 0) {
        font_draw(ren, "X type  [] del  /\\ space  R2 search  R1 latest  L2 category  L1 shift  O close",
                  PX + 16, ry + UI_SEARCH_PAGE * ROW_H + 4, 1, DIM);
        osk_draw(ren, &u->kbd);
    } else {
        footer(ren, "X open   /\\ favourite   O back to typing   L1/R1 page");
    }
}

static void draw_a64_files(SDL_Renderer *ren, const ui_state *u, Uint32 now)
{
    char title[160];
    snprintf(title, sizeof(title), "%s%s%s", u->a64_cur.name, u->a64_cur.group[0] ? " - " : "",
             u->a64_cur.group);
    char t[128];
    fit(title, CHARS, t, sizeof(t));
    panel(ren, t);
    if (u->a64_loading || u->a64_status[0]) {
        char line[200];
        snprintf(line, sizeof(line), "%s", u->a64_status);
        if (u->a64_loading)
            snprintf(line, sizeof(line), "%c %s", "|/-\\"[(now / 150) % 4], u->a64_status);
        font_draw(ren, line, PX + 16, PY + 38, 1, u->a64_loading ? YELLOW : DIM);
    }
    for (int i = u->a64_ftop; i < u->a64_nfiles && i < u->a64_ftop + UI_PAGE; i++) {
        const a64_file *f = &u->a64_files[i];
        int type = files_type(f->path, 0);
        char name[128], size[16], line[160];
        fit(f->path, CHARS - 16, name, sizeof(name));
        if (f->size >= 1024 * 1024)
            snprintf(size, sizeof(size), "%lluM", f->size / (1024 * 1024));
        else
            snprintf(size, sizeof(size), "%lluK", (f->size + 1023) / 1024);
        snprintf(line, sizeof(line), "%c%-4s %-*s %7s", ui_is_fav_a64(u, &u->a64_cur, f) ? STAR : ' ',
                 files_type_tag(type), CHARS - 16, name, size);
        row(ren, i - u->a64_ftop, i == u->a64_fsel, line, type == FT_OTHER ? DIM : WHITE);
    }
    footer(ren, "X run   [] more   /\\ favourite   O back to results");
}

/* ---- favourites ---------------------------------------------------------------- */

static void draw_favs(SDL_Renderer *ren, const ui_state *u)
{
    panel(ren, "Favourites");
    char games[48], sids[48];
    snprintf(games, sizeof(games), "Games (%d)", u->favs ? fav_count_group(u->favs, FAV_GAMES) : 0);
    snprintf(sids, sizeof(sids), "SIDs (%d)", u->favs ? fav_count_group(u->favs, FAV_SIDS) : 0);
    row(ren, 0, u->sel == 0, games, CYAN);
    row(ren, 1, u->sel == 1, sids, CYAN);
    font_draw(ren, "Add favourites with Triangle in the file browser, search, Assembly64",
              PX + 16, LIST_Y + 3 * ROW_H, 1, DIM);
    font_draw(ren, "or Recents.", PX + 16, LIST_Y + 4 * ROW_H, 1, DIM);
    footer(ren, "X open   O close");
}

static const char *fav_tag(const fav_entry *e)
{
    if (fav_is_folder(e)) return "DIR";
    if (e->kind == FAV_A64_ITEM) return "A64";
    return files_type_tag(files_type(e->path, 0));
}

static void draw_fav_list(SDL_Renderer *ren, const ui_state *u)
{
    panel(ren, u->fav_recents ? "Recents" : u->fav_group == FAV_SIDS ? "Favourites: SIDs"
                                                                      : "Favourites: Games");
    int rows = ui_fav_rows(u);
    if (!rows)
        font_draw(ren, u->fav_recents ? "Nothing launched yet." : "No favourites here yet.",
                  PX + 16, LIST_Y, 1, DIM);
    for (int i = u->fl_top; i < rows && i < u->fl_top + UI_PAGE; i++) {
        const fav_entry *e = ui_fav_row(u, i);
        char where[256], name[96], place[64], line[200];
        if (e->kind == FAV_A64_FILE || e->kind == FAV_A64_ITEM) {
            snprintf(where, sizeof(where), "Assembly64");
        } else if (fav_is_emulator(e) && !files_emulator()) {
            snprintf(where, sizeof(where), "Emulator");     /* not this machine's */
        } else {
            /* The folder the title lives in, shortened from the left. */
            snprintf(where, sizeof(where), "%s", e->path);
            char *cut = strrchr(where, '/');
            if (cut && !fav_is_folder(e))
                *cut = 0, cut = strrchr(where, '/');
            if (cut)
                *cut = 0;
        }
        fit(e->name, 44, name, sizeof(name));
        fit_left(where, CHARS - 52, place, sizeof(place));
        int starred = u->fav_recents && u->favs && fav_find(u->favs, e) >= 0;
        snprintf(line, sizeof(line), "%c%-4s %-44s %s", starred ? STAR : ' ', fav_tag(e), name, place);
        row(ren, i - u->fl_top, i == u->fl_sel, line, WHITE);
    }
    footer(ren, u->fav_recents ? "X launch   [] more   /\\ favourite   O close"
                               : "X launch   [] more   /\\ remove   O back");
}

void ui_draw(SDL_Renderer *ren, const ui_state *u, Uint32 now)
{
    if (u->screen == UI_CLOSED)
        return;
    int saved_w, saved_h;
    canvas_begin(ren, &saved_w, &saved_h);
    switch (u->screen) {
    case UI_MENU:     draw_menu(ren, u); break;
    case UI_PICKER:   draw_picker(ren, u, now); break;
    case UI_ADDRESS:  draw_address(ren, u); break;
    case UI_PASSWORD: draw_password(ren, u); break;
    case UI_BROWSER:  draw_browser(ren, u); break;
    case UI_ACTIONS:  draw_actions(ren, u, now); break;
    case UI_SEARCH:   draw_search(ren, u, now); break;
    case UI_A64:      draw_a64(ren, u, now); break;
    case UI_A64_FILES: draw_a64_files(ren, u, now); break;
    case UI_A64_ID:   draw_a64_id(ren, u, now); break;
    case UI_FAVS:     draw_favs(ren, u); break;
    case UI_FAV_LIST: draw_fav_list(ren, u); break;
    case UI_ABOUT:    draw_about(ren, u); break;
    }
    canvas_end(ren, saved_w, saved_h);
}
