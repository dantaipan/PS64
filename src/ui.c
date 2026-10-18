#include "ui.h"

#include "c64keys.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>

static ui_action none(void)
{
    ui_action a;
    memset(&a, 0, sizeof(a));
    a.kind = UA_NONE;
    return a;
}

static ui_action act(int kind)
{
    ui_action a = none();
    a.kind = kind;
    return a;
}

void ui_setup(ui_state *u)
{
    memset(u, 0, sizeof(*u));
    u->screen = UI_CLOSED;
    u->port = 2;
    u->a64_cat = -1;                    /* all categories */
    snprintf(u->path, sizeof(u->path), "/");
}

void ui_free(ui_state *u)
{
    free(u->entries);
    u->entries = NULL;
    u->count = 0;
    free(u->a64_items);
    free(u->a64_files);
    u->a64_items = NULL;
    u->a64_files = NULL;
    u->a64_count = u->a64_nfiles = 0;
}

static void go(ui_state *u, int screen)
{
    u->screen = screen;
    u->sel = 0;
    u->top = 0;
}

void ui_open_menu(ui_state *u)  { go(u, UI_MENU); }
void ui_close(ui_state *u)      { u->screen = UI_CLOSED; }

void ui_open_picker(ui_state *u, const char *message)
{
    go(u, UI_PICKER);
    snprintf(u->message, sizeof(u->message), "%s", message ? message : "");
}

ui_action ui_open_browser(ui_state *u, const char *path)
{
    go(u, UI_BROWSER);
    snprintf(u->path, sizeof(u->path), "%s", path && *path ? path : "/");
    u->loading = 1;
    u->error[0] = 0;
    ui_action a = act(UA_LIST);
    snprintf(a.path, sizeof(a.path), "%s", u->path);
    return a;
}

int ui_picker_rows(const ui_state *u)
{
    return u->ndevices + 4;     /* + Enter address, Search again, Built-in emulator, Listen only */
}

void ui_set_devices(ui_state *u, const c64_device *d, int n, int progress, int done)
{
    memcpy(u->devices, d, sizeof(*d) * (size_t)n);
    u->ndevices = n;
    u->progress = progress;
    u->scanning = !done;
}

void ui_set_listing(ui_state *u, const char *path, ftp_entry *entries, int count)
{
    free(u->entries);
    u->entries = entries;
    u->count = count;
    u->loading = 0;
    u->error[0] = 0;
    /* Coming back up a level: keep the folder we came from selected. */
    int sel = 0;
    size_t plen = strlen(path);
    if (!strncmp(u->path, path, plen) && strlen(u->path) > plen) {
        const char *child = u->path + plen + (u->path[plen] == '/' ? 1 : 0);
        for (int i = 0; i < count; i++)
            if (!strcmp(entries[i].name, child))
                sel = i;
    }
    snprintf(u->path, sizeof(u->path), "%s", path);
    u->sel = sel;
    u->top = sel >= UI_PAGE ? sel - UI_PAGE / 2 : 0;
}

void ui_set_list_error(ui_state *u, const char *path, const char *message)
{
    u->loading = 0;
    snprintf(u->path, sizeof(u->path), "%s", path);
    snprintf(u->error, sizeof(u->error), "%s", message);
    free(u->entries);
    u->entries = NULL;
    u->count = 0;
    u->sel = u->top = 0;
}

void ui_parent(const char *path, char *out, size_t cap)
{
    snprintf(out, cap, "%s", path);
    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/')
        out[--n] = 0;
    char *slash = strrchr(out, '/');
    if (!slash || slash == out)
        snprintf(out, cap, "/");
    else
        *slash = 0;
}

/* Move a list selection with wrap-around and keep it on screen. */
static void move(ui_state *u, int rows, int delta, int wrap)
{
    if (rows <= 0)
        return;
    int s = u->sel + delta;
    if (wrap)
        s = (s % rows + rows) % rows;
    else
        s = s < 0 ? 0 : s >= rows ? rows - 1 : s;
    u->sel = s;
    if (u->sel < u->top)
        u->top = u->sel;
    if (u->sel >= u->top + UI_PAGE)
        u->top = u->sel - UI_PAGE + 1;
}

static void start_address(ui_state *u)
{
    /* Pre-fill from the device list or the current C64's /24. */
    const char *seed = u->ndevices ? u->devices[0].ip : u->current_ip[0] ? u->current_ip : "192.168.1.64";
    struct in_addr a;
    unsigned long v = inet_pton(AF_INET, seed, &a) == 1 ? ntohl(a.s_addr) : 0xC0A80140UL;
    for (int i = 0; i < 4; i++)
        u->octets[i] = (int)((v >> (24 - 8 * i)) & 0xFF);
    u->field = 3;
    go(u, UI_ADDRESS);
}

static ui_action connect_to(ui_state *u, const char *ip, const char *password)
{
    ui_action a = act(UA_CONNECT);
    snprintf(a.ip, sizeof(a.ip), "%s", ip);
    snprintf(a.password, sizeof(a.password), "%s", password ? password : "");
    ui_close(u);
    return a;
}

/* Password entry: PETSCII from the on-screen keyboard back to ASCII. */
static int petscii_to_ascii(int p)
{
    if (p >= 0x41 && p <= 0x5A) return p + 0x20;    /* plain letters -> lower case */
    if (p >= 0xC1 && p <= 0xDA) return p - 0x80;    /* shifted letters -> upper case */
    if (p >= 0x20 && p <= 0x3F) return p;
    if (p == 0x40 || p == 0x5B || p == 0x5D) return p;
    return -1;
}

int ui_menu_items(const ui_state *u, int *out)
{
    static const int ultimate[] = { MI_RESUME, MI_BROWSE, MI_SEARCH, MI_A64, MI_FAVS, MI_RECENTS,
                                    MI_RESET, MI_REBOOT, MI_AUDIO, MI_PORT, MI_CHOOSE, MI_INDEX,
                                    MI_QUIT };
    static const int emulator[] = { MI_RESUME, MI_BROWSE, MI_SEARCH, MI_A64, MI_FAVS, MI_RECENTS,
                                    MI_DISK, MI_RESET, MI_REBOOT, MI_CONTROLLER, MI_AUDIO, MI_PORT,
                                    MI_CHOOSE, MI_INDEX, MI_QUIT };
    const int *src = u->emulator ? emulator : ultimate;
    int n = u->emulator ? (int)(sizeof(emulator) / sizeof(emulator[0]))
                        : (int)(sizeof(ultimate) / sizeof(ultimate[0]));
    int count = 0;
    for (int i = 0; i < n; i++)
        if (src[i] != MI_DISK || u->disks > 1)
            out[count++] = src[i];
    return count;
}

static ui_action input_menu(ui_state *u, int in)
{
    int items[MI_COUNT];
    int n = ui_menu_items(u, items);
    if (u->sel >= n)
        u->sel = n - 1;
    int item = items[u->sel];
    switch (in) {
    case IN_UP:   move(u, n, -1, 1); break;
    case IN_DOWN: move(u, n, 1, 1); break;
    case IN_BACK: ui_close(u); break;
    case IN_LEFT: case IN_RIGHT: case IN_OK:
        switch (item) {
        case MI_AUDIO: return act(UA_MUTE);
        case MI_PORT:  return act(UA_PORT);
        case MI_CONTROLLER: return act(UA_CONTROLLER);
        case MI_DISK: {
            ui_action a = act(UA_DISK);
            a.offset = in == IN_LEFT ? -1 : 1;
            return a;
        }
        }
        if (in != IN_OK)
            break;
        switch (item) {
        case MI_RESUME: ui_close(u); break;
        case MI_BROWSE: return act(UA_OPEN_BROWSER);
        case MI_SEARCH: return act(UA_OPEN_SEARCH);
        case MI_A64:    ui_open_a64(u); break;
        case MI_FAVS:   ui_open_favs(u); break;
        case MI_RECENTS: ui_open_recents(u); break;
        case MI_INDEX:  return act(UA_BUILD_INDEX);
        case MI_RESET:  ui_close(u); return act(UA_RESET);
        case MI_REBOOT: ui_close(u); return act(UA_REBOOT);
        case MI_CHOOSE: ui_open_picker(u, NULL); return act(UA_RESCAN);
        case MI_QUIT:   return act(UA_QUIT);
        }
        break;
    }
    return none();
}

static ui_action input_picker(ui_state *u, int in)
{
    int rows = ui_picker_rows(u);
    switch (in) {
    case IN_UP:   move(u, rows, -1, 1); break;
    case IN_DOWN: move(u, rows, 1, 1); break;
    case IN_BACK: ui_close(u); break;
    case IN_OK:
        if (u->sel < u->ndevices) {
            c64_device *d = &u->devices[u->sel];
            if (d->locked) {
                snprintf(u->pending_ip, sizeof(u->pending_ip), "%s", d->ip);
                u->password[0] = 0;
                osk_init(&u->kbd);
                go(u, UI_PASSWORD);
                break;
            }
            return connect_to(u, d->ip, NULL);
        }
        switch (u->sel - u->ndevices) {
        case 0: start_address(u); break;
        case 1: u->message[0] = 0; return act(UA_RESCAN);
        case 2: ui_close(u); return act(UA_EMULATOR);
        case 3: ui_close(u); return act(UA_LISTEN_ONLY);
        }
        break;
    }
    return none();
}

static ui_action input_address(ui_state *u, int in)
{
    int *o = &u->octets[u->field];
    switch (in) {
    case IN_LEFT:  u->field = (u->field + 3) % 4; break;
    case IN_RIGHT: u->field = (u->field + 1) % 4; break;
    case IN_UP:    *o = (*o + 1) % 256; break;
    case IN_DOWN:  *o = (*o + 255) % 256; break;
    case IN_R1:    *o = (*o + 10) % 256; break;
    case IN_L1:    *o = (*o + 246) % 256; break;
    case IN_BACK:  go(u, UI_PICKER); break;
    case IN_OK: {
        char ip[16];
        snprintf(ip, sizeof(ip), "%d.%d.%d.%d", u->octets[0], u->octets[1], u->octets[2], u->octets[3]);
        return connect_to(u, ip, NULL);
    }
    }
    return none();
}

static ui_action input_password(ui_state *u, int in)
{
    size_t len = strlen(u->password);
    switch (in) {
    case IN_UP:    osk_move(&u->kbd, 0, -1); break;
    case IN_DOWN:  osk_move(&u->kbd, 0, 1); break;
    case IN_LEFT:  osk_move(&u->kbd, -1, 0); break;
    case IN_RIGHT: osk_move(&u->kbd, 1, 0); break;
    case IN_L1:    osk_toggle_shift(&u->kbd); break;
    case IN_ALT:   if (len) u->password[len - 1] = 0; break;
    case IN_BACK:  go(u, UI_PICKER); break;
    case IN_R2:    return connect_to(u, u->pending_ip, u->password);
    case IN_OK: {
        int key, mods;
        if (!osk_press(&u->kbd, &key, &mods))
            break;
        if (key == C64K_RETURN)
            return connect_to(u, u->pending_ip, u->password);
        if (key == C64K_INST_DEL) {
            if (len) u->password[len - 1] = 0;
            break;
        }
        int ch = petscii_to_ascii(c64key_petscii(key, mods));
        if (ch > 0 && len + 1 < sizeof(u->password)) {
            u->password[len] = (char)ch;
            u->password[len + 1] = 0;
        }
        break;
    }
    }
    return none();
}

static void child_path(const ui_state *u, const char *name, char *out, size_t cap)
{
    snprintf(out, cap, "%s%s%s", u->path, strcmp(u->path, "/") ? "/" : "", name);
}

/* ---- favourites: entries ------------------------------------------------------ */

static fav_entry entry_local(const char *path, int is_dir)
{
    return fav_local(path, is_dir);
}

static fav_entry entry_a64_item(const ui_state *u, const a64_item *it)
{
    fav_entry e;
    memset(&e, 0, sizeof(e));
    e.kind = FAV_A64_ITEM;
    const char *cat = a64_category_name(&u->a64_cats, it->category);
    for (int i = 0; !cat && i < u->a64_nseen; i++)
        if (u->a64_seen_num[i] == it->category)
            cat = u->a64_seen_name[i];
    e.group = fav_group_for_category(cat);
    e.action = FA_NONE;
    snprintf(e.item_id, sizeof(e.item_id), "%s", it->id);
    e.category = it->category;
    snprintf(e.name, sizeof(e.name), "%s", it->name);
    return e;
}

static fav_entry entry_a64_file(const a64_item *it, const a64_file *f)
{
    return fav_a64_file(it->id, it->category, f->id, f->path, it->name);
}

static ui_action fav_toggle_action(fav_entry e)
{
    ui_action a = act(UA_FAV_TOGGLE);
    a.fav = e;
    return a;
}

int ui_is_fav_local(const ui_state *u, const char *path, int is_dir)
{
    if (!u->favs)
        return 0;
    fav_entry e = entry_local(path, is_dir);
    return fav_find(u->favs, &e) >= 0;
}

int ui_is_fav_a64(const ui_state *u, const a64_item *item, const a64_file *file)
{
    if (!u->favs)
        return 0;
    fav_entry e = file ? entry_a64_file(item, file) : entry_a64_item(u, item);
    return fav_find(u->favs, &e) >= 0;
}

static void child_path(const ui_state *u, const char *name, char *out, size_t cap);

static ui_action input_browser(ui_state *u, int in)
{
    if (in == IN_L2)
        return act(UA_OPEN_SEARCH);
    if (in == IN_TRIANGLE) {
        if (u->loading || u->sel >= u->count)
            return none();
        char path[600];
        child_path(u, u->entries[u->sel].name, path, sizeof(path));
        return fav_toggle_action(entry_local(path, u->entries[u->sel].is_dir));
    }
    if (u->loading && in != IN_BACK)
        return none();
    switch (in) {
    case IN_UP:   move(u, u->count, -1, 1); break;
    case IN_DOWN: move(u, u->count, 1, 1); break;
    case IN_L1:   move(u, u->count, -UI_PAGE, 0); break;
    case IN_R1:   move(u, u->count, UI_PAGE, 0); break;
    case IN_BACK:
    case IN_LEFT:
        if (!strcmp(u->path, "/")) {
            if (in == IN_BACK)
                ui_close(u);
            break;
        } else {
            ui_action a = act(UA_LIST);
            ui_parent(u->path, a.path, sizeof(a.path));
            u->loading = 1;
            return a;
        }
    case IN_OK:
    case IN_RIGHT:
    case IN_ALT: {
        if (u->sel >= u->count)
            break;
        const ftp_entry *e = &u->entries[u->sel];
        int type = files_type(e->name, e->is_dir);
        if (type == FT_DIR) {
            ui_action a = act(UA_LIST);
            child_path(u, e->name, a.path, sizeof(a.path));
            u->loading = 1;
            return a;
        }
        if (in == IN_RIGHT)
            break;
        if (in == IN_ALT) {
            u->nactions = files_actions_for(type, u->actions, FA_COUNT);
            u->action_sel = 0;
            child_path(u, e->name, u->action_path, sizeof(u->action_path));
            u->action_return = UI_BROWSER;
            if (u->nactions)
                u->screen = UI_ACTIONS;
            break;
        }
        int fa = files_default_action(type);
        if (fa == FA_NONE)
            break;
        ui_action a = act(UA_FILE);
        child_path(u, e->name, a.path, sizeof(a.path));
        a.file_action = fa;
        return a;
    }
    }
    return none();
}

/* ---- Assembly64 -------------------------------------------------------------- */

/*
 * Typing into buf with the on-screen keyboard: D-pad moves, Cross types,
 * Square deletes, Triangle types a space, L1 is Shift. Returns 1 when the
 * keyboard's RETURN key was pressed; *changed is set when buf changed.
 */
static int kbd_text(ui_state *u, char *buf, size_t cap, int in, int *changed)
{
    size_t len = strlen(buf);
    *changed = 0;
    switch (in) {
    case IN_UP:    osk_move(&u->kbd, 0, -1); break;
    case IN_DOWN:  osk_move(&u->kbd, 0, 1); break;
    case IN_LEFT:  osk_move(&u->kbd, -1, 0); break;
    case IN_RIGHT: osk_move(&u->kbd, 1, 0); break;
    case IN_L1:    osk_toggle_shift(&u->kbd); break;
    case IN_ALT:
        if (len) { buf[len - 1] = 0; *changed = 1; }
        break;
    case IN_TRIANGLE:
        if (len + 1 < cap) { buf[len] = ' '; buf[len + 1] = 0; *changed = 1; }
        break;
    case IN_OK: {
        int key, mods;
        if (!osk_press(&u->kbd, &key, &mods))
            break;
        if (key == C64K_RETURN)
            return 1;
        if (key == C64K_INST_DEL) {
            if (len) { buf[len - 1] = 0; *changed = 1; }
            break;
        }
        int ch = petscii_to_ascii(c64key_petscii(key, mods));
        if (ch > 0 && len + 1 < cap) { buf[len] = (char)ch; buf[len + 1] = 0; *changed = 1; }
        break;
    }
    }
    return 0;
}

static void kbd_open(ui_state *u)
{
    if (!u->kbd.row && !u->kbd.col)
        osk_init(&u->kbd);
    u->kbd.open = 1;
}

void ui_open_a64_id(ui_state *u, const char *message)
{
    go(u, UI_A64_ID);
    kbd_open(u);
    snprintf(u->a64_status, sizeof(u->a64_status), "%s", message ? message : "");
}

void ui_open_a64(ui_state *u)
{
    if (!u->a64_ready) {
        ui_open_a64_id(u, NULL);
        return;
    }
    u->screen = UI_A64;
    u->a64_focus = u->a64_count ? 1 : 0;
    kbd_open(u);
}

static const char *a64_cat_key(const ui_state *u)
{
    return u->a64_cat >= 0 && u->a64_cat < u->a64_cats.count ? u->a64_cats.keys[u->a64_cat] : "";
}

static ui_action a64_search_action(ui_state *u, int offset)
{
    ui_action a = act(UA_A64_SEARCH);
    snprintf(a.text, sizeof(a.text), "%s", u->a64_query);
    snprintf(a.category_key, sizeof(a.category_key), "%s", a64_cat_key(u));
    snprintf(u->a64_search_cat, sizeof(u->a64_search_cat), "%s",
             u->a64_cat >= 0 && u->a64_cat < u->a64_cats.count ? u->a64_cats.names[u->a64_cat] : "");
    a.offset = offset;
    u->a64_loading = 1;
    snprintf(u->a64_status, sizeof(u->a64_status), "Searching Assembly64...");
    return a;
}

static ui_action a64_file_action(ui_state *u, int file_action)
{
    const a64_file *f = &u->a64_files[u->a64_fsel];
    ui_action a = act(UA_A64_FILE);
    snprintf(a.text, sizeof(a.text), "%s", u->a64_cur.id);
    a.category = u->a64_cur.category;
    a.file_id = f->id;
    snprintf(a.path, sizeof(a.path), "%s", f->path);
    a.file_action = file_action;
    snprintf(u->a64_status, sizeof(u->a64_status), "Downloading %s...", f->path);
    return a;
}

void ui_a64_results(ui_state *u, a64_result *r)
{
    u->a64_loading = 0;
    if (r->kind == A64R_SEARCH) {
        /* A filtered search tells us what its results' category numbers mean. */
        for (int i = 0; u->a64_search_cat[0] && i < r->count; i++) {
            int known = 0;
            for (int j = 0; j < u->a64_nseen; j++)
                known |= u->a64_seen_num[j] == r->items[i].category;
            if (!known && u->a64_nseen < A64_MAX_CATS) {
                u->a64_seen_num[u->a64_nseen] = r->items[i].category;
                snprintf(u->a64_seen_name[u->a64_nseen], sizeof(u->a64_seen_name[0]), "%s",
                         u->a64_search_cat);
                u->a64_nseen++;
            }
        }
        if (r->offset == 0) {
            free(u->a64_items);
            u->a64_items = r->items;
            u->a64_count = r->count;
            u->a64_sel = u->a64_top = 0;
        } else {
            a64_item *all = realloc(u->a64_items, sizeof(*all) * (size_t)(u->a64_count + r->count));
            if (all) {
                memcpy(all + u->a64_count, r->items, sizeof(*all) * (size_t)r->count);
                u->a64_items = all;
                u->a64_sel = u->a64_count;          /* first new result */
                u->a64_count += r->count;
            }
            free(r->items);
        }
        r->items = NULL;
        u->a64_more = r->count == A64_PAGE;
        snprintf(u->a64_status, sizeof(u->a64_status), u->a64_count ? "%d result%s%s"
                 : "No results%.0d%.0s%.0s", u->a64_count, u->a64_count == 1 ? "" : "s",
                 u->a64_more ? " so far" : "");
        if (u->screen == UI_A64 && u->a64_count)
            u->a64_focus = 1;
    } else if (r->kind == A64R_ENTRIES) {
        free(u->a64_files);
        u->a64_files = r->files;
        u->a64_nfiles = r->nfiles;
        r->files = NULL;
        u->a64_fsel = u->a64_ftop = 0;
        snprintf(u->a64_status, sizeof(u->a64_status), u->a64_nfiles ? "" : "This entry has no files");
    } else if (r->kind == A64R_CATEGORIES) {
        u->a64_cats = r->cats;
        if (u->a64_cat >= u->a64_cats.count)
            u->a64_cat = -1;
    }
}

void ui_a64_error(ui_state *u, const char *message, int auth_error)
{
    u->a64_loading = 0;
    snprintf(u->a64_status, sizeof(u->a64_status), "%s", message);
    if (auth_error && (u->screen == UI_A64 || u->screen == UI_A64_FILES))
        ui_open_a64_id(u, message);
}

static void move_in(int *sel, int *top, int rows, int page, int delta, int wrap)
{
    if (rows <= 0)
        return;
    int s = *sel + delta;
    if (wrap)
        s = (s % rows + rows) % rows;
    else
        s = s < 0 ? 0 : s >= rows ? rows - 1 : s;
    *sel = s;
    if (*sel < *top)
        *top = *sel;
    if (*sel >= *top + page)
        *top = *sel - page + 1;
}

static ui_action input_a64_id(ui_state *u, int in)
{
    int changed;
    if (in == IN_BACK) {
        ui_close(u);
        return none();
    }
    if ((in == IN_R2 || kbd_text(u, u->a64_id, sizeof(u->a64_id), in, &changed)) && u->a64_id[0]) {
        ui_action a = act(UA_A64_SET_ID);
        snprintf(a.text, sizeof(a.text), "%s", u->a64_id);
        u->a64_status[0] = 0;
        u->screen = UI_A64;
        u->a64_focus = 0;
        return a;
    }
    return none();
}

static ui_action input_a64(ui_state *u, int in)
{
    if (u->a64_focus == 0) {
        int changed;
        if (in == IN_BACK) {
            ui_close(u);
            return none();
        }
        if (in == IN_L2) {
            u->a64_cat = u->a64_cat + 1 >= u->a64_cats.count ? -1 : u->a64_cat + 1;
            return none();
        }
        int submit = in == IN_R2 || kbd_text(u, u->a64_query, sizeof(u->a64_query), in, &changed);
        if (submit && u->a64_query[0] && !u->a64_loading)
            return a64_search_action(u, 0);
        return none();
    }

    int rows = u->a64_count + (u->a64_more ? 1 : 0);
    switch (in) {
    case IN_UP:   move_in(&u->a64_sel, &u->a64_top, rows, UI_SEARCH_PAGE, -1, 1); break;
    case IN_DOWN: move_in(&u->a64_sel, &u->a64_top, rows, UI_SEARCH_PAGE, 1, 1); break;
    case IN_L1:   move_in(&u->a64_sel, &u->a64_top, rows, UI_SEARCH_PAGE, -UI_SEARCH_PAGE, 0); break;
    case IN_R1:   move_in(&u->a64_sel, &u->a64_top, rows, UI_SEARCH_PAGE, UI_SEARCH_PAGE, 0); break;
    case IN_BACK:
    case IN_R2:   u->a64_focus = 0; break;
    case IN_TRIANGLE:
        if (u->a64_sel < u->a64_count)
            return fav_toggle_action(entry_a64_item(u, &u->a64_items[u->a64_sel]));
        break;
    case IN_OK:
        if (u->a64_loading)
            break;
        if (u->a64_sel == u->a64_count && u->a64_more)
            return a64_search_action(u, u->a64_count);       /* More results... */
        if (u->a64_sel < u->a64_count) {
            u->a64_cur = u->a64_items[u->a64_sel];
            ui_action a = act(UA_A64_ENTRIES);
            snprintf(a.text, sizeof(a.text), "%s", u->a64_cur.id);
            a.category = u->a64_cur.category;
            free(u->a64_files);
            u->a64_files = NULL;
            u->a64_nfiles = 0;
            u->a64_loading = 1;
            snprintf(u->a64_status, sizeof(u->a64_status), "Loading files...");
            go(u, UI_A64_FILES);
            return a;
        }
        break;
    }
    return none();
}

static ui_action input_a64_files(ui_state *u, int in)
{
    if (in == IN_BACK) {
        u->screen = UI_A64;
        u->a64_focus = 1;
        u->a64_loading = 0;
        return none();
    }
    if (u->a64_loading || !u->a64_nfiles)
        return none();
    const a64_file *f = &u->a64_files[u->a64_fsel];
    int type = files_type(f->path, 0);
    switch (in) {
    case IN_UP:   move_in(&u->a64_fsel, &u->a64_ftop, u->a64_nfiles, UI_PAGE, -1, 1); break;
    case IN_DOWN: move_in(&u->a64_fsel, &u->a64_ftop, u->a64_nfiles, UI_PAGE, 1, 1); break;
    case IN_TRIANGLE:
        return fav_toggle_action(entry_a64_file(&u->a64_cur, f));
    case IN_OK: {
        int fa = files_default_action(type);
        if (fa != FA_NONE)
            return a64_file_action(u, fa);
        break;
    }
    case IN_ALT:
        u->nactions = files_actions_for(type, u->actions, FA_COUNT);
        u->action_sel = 0;
        snprintf(u->action_path, sizeof(u->action_path), "%s", f->path);
        u->action_return = UI_A64_FILES;
        if (u->nactions)
            u->screen = UI_ACTIONS;
        break;
    }
    return none();
}

static ui_action input_actions(ui_state *u, int in)
{
    switch (in) {
    case IN_UP:   u->action_sel = (u->action_sel + u->nactions - 1) % u->nactions; break;
    case IN_DOWN: u->action_sel = (u->action_sel + 1) % u->nactions; break;
    case IN_BACK:
    case IN_ALT:  u->screen = u->action_return; break;
    case IN_OK: {
        u->screen = u->action_return;
        if (u->action_return == UI_A64_FILES)
            return a64_file_action(u, u->actions[u->action_sel]);
        if (u->action_return == UI_FAV_LIST) {
            ui_action a = act(UA_FAV_LAUNCH);
            a.fav = u->action_fav;
            a.fav.action = u->actions[u->action_sel];
            return a;
        }
        ui_action a = act(UA_FILE);
        snprintf(a.path, sizeof(a.path), "%s", u->action_path);
        a.file_action = u->actions[u->action_sel];
        return a;
    }
    }
    return none();
}

/* ---- search ------------------------------------------------------------------ */

void ui_search_refresh(ui_state *u)
{
    u->nresults = u->index && u->query[0]
                  ? index_search(u->index, u->query, u->results, UI_SEARCH_MAX) : 0;
    if (u->rsel >= u->nresults)
        u->rsel = u->nresults ? u->nresults - 1 : 0;
    if (u->rtop > u->rsel)
        u->rtop = u->rsel;
}

void ui_open_search(ui_state *u)
{
    u->screen = UI_SEARCH;
    u->search_focus = 0;
    u->rsel = u->rtop = 0;
    if (!u->kbd.row && !u->kbd.col)
        osk_init(&u->kbd);
    u->kbd.open = 1;
    ui_search_refresh(u);
}

static void query_changed(ui_state *u)
{
    u->rsel = u->rtop = 0;
    ui_search_refresh(u);
}

/* Append printable ASCII from a real keyboard to buf. */
static int append_text(char *buf, size_t cap, const char *text)
{
    size_t len = strlen(buf);
    int changed = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
        if (*p >= 0x20 && *p < 0x7F && len + 1 < cap) {
            buf[len++] = (char)*p;
            changed = 1;
        }
    buf[len] = 0;
    return changed;
}

void ui_text(ui_state *u, const char *text)
{
    if (u->screen == UI_A64_ID) {
        append_text(u->a64_id, sizeof(u->a64_id), text);
        return;
    }
    if (u->screen == UI_A64 ) {
        u->a64_focus = 0;
        append_text(u->a64_query, sizeof(u->a64_query), text);
        return;
    }
    if (u->screen != UI_SEARCH)
        return;
    size_t len = strlen(u->query);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
        if (*p >= 0x20 && *p < 0x7F && len + 1 < sizeof(u->query))
            u->query[len++] = (char)*p;
    u->query[len] = 0;
    u->search_focus = 0;
    query_changed(u);
}

static void move_results(ui_state *u, int delta, int wrap)
{
    if (!u->nresults)
        return;
    int s = u->rsel + delta;
    if (wrap)
        s = (s % u->nresults + u->nresults) % u->nresults;
    else
        s = s < 0 ? 0 : s >= u->nresults ? u->nresults - 1 : s;
    u->rsel = s;
    if (u->rsel < u->rtop)
        u->rtop = u->rsel;
    if (u->rsel >= u->rtop + UI_SEARCH_PAGE)
        u->rtop = u->rsel - UI_SEARCH_PAGE + 1;
}

static ui_action input_search(ui_state *u, int in)
{
    /* No index yet: offer to build one. */
    if (u->index && index_count(u->index) == 0) {
        index_status st;
        index_status_get(u->index, &st);
        if (!st.building) {
            if (in == IN_OK)
                return act(UA_BUILD_INDEX);
            if (in == IN_BACK)
                ui_close(u);
            return none();
        }
    }

    size_t len = strlen(u->query);
    if (u->search_focus == 0) {
        switch (in) {
        case IN_UP:    osk_move(&u->kbd, 0, -1); break;
        case IN_DOWN:  osk_move(&u->kbd, 0, 1); break;
        case IN_LEFT:  osk_move(&u->kbd, -1, 0); break;
        case IN_RIGHT: osk_move(&u->kbd, 1, 0); break;
        case IN_L1:    osk_toggle_shift(&u->kbd); break;
        case IN_BACK:  ui_close(u); break;
        case IN_R2:
            if (u->nresults)
                u->search_focus = 1;
            break;
        case IN_ALT:
            if (len) {
                u->query[len - 1] = 0;
                query_changed(u);
            }
            break;
        case IN_TRIANGLE:
            if (len + 1 < sizeof(u->query)) {
                u->query[len] = ' ';
                u->query[len + 1] = 0;
            }
            break;
        case IN_OK: {
            int key, mods;
            if (!osk_press(&u->kbd, &key, &mods))
                break;
            if (key == C64K_RETURN) {
                if (u->nresults)
                    u->search_focus = 1;
                break;
            }
            if (key == C64K_INST_DEL) {
                if (len) {
                    u->query[len - 1] = 0;
                    query_changed(u);
                }
                break;
            }
            int ch = petscii_to_ascii(c64key_petscii(key, mods));
            if (ch > 0 && len + 1 < sizeof(u->query)) {
                u->query[len] = (char)ch;
                u->query[len + 1] = 0;
                query_changed(u);
            }
            break;
        }
        }
        return none();
    }

    /* Results focus. */
    if (!u->nresults) {
        u->search_focus = 0;
        return none();
    }
    const index_hit *h = &u->results[u->rsel];
    switch (in) {
    case IN_UP:   move_results(u, -1, 1); break;
    case IN_DOWN: move_results(u, 1, 1); break;
    case IN_L1:   move_results(u, -UI_SEARCH_PAGE, 0); break;
    case IN_R1:   move_results(u, UI_SEARCH_PAGE, 0); break;
    case IN_BACK:
    case IN_R2:   u->search_focus = 0; break;
    case IN_OK: {
        int fa = files_default_action(h->type);
        if (fa == FA_NONE)
            break;
        ui_action a = act(UA_FILE);
        snprintf(a.path, sizeof(a.path), "%s", h->path);
        a.file_action = fa;
        return a;
    }
    case IN_TRIANGLE:
        return fav_toggle_action(entry_local(h->path, 0));
    case IN_ALT:
        u->nactions = files_actions_for(h->type, u->actions, FA_COUNT);
        u->action_sel = 0;
        snprintf(u->action_path, sizeof(u->action_path), "%s", h->path);
        u->action_return = UI_SEARCH;
        if (u->nactions)
            u->screen = UI_ACTIONS;
        break;
    case IN_RIGHT: {
        /* Show the file in its folder: the listing selects it on arrival. */
        ui_action a = act(UA_LIST);
        ui_parent(h->path, a.path, sizeof(a.path));
        go(u, UI_BROWSER);
        snprintf(u->path, sizeof(u->path), "%s", h->path);
        u->loading = 1;
        u->error[0] = 0;
        return a;
    }
    }
    return none();
}

/* ---- favourites: screens ------------------------------------------------------ */

void ui_open_favs(ui_state *u)
{
    go(u, UI_FAVS);
}

void ui_open_recents(ui_state *u)
{
    u->screen = UI_FAV_LIST;
    u->fav_recents = 1;
    u->fl_sel = u->fl_top = 0;
}

int ui_fav_rows(const ui_state *u)
{
    if (u->fav_recents)
        return u->recents ? u->recents->count : 0;
    return u->favs ? fav_count_group(u->favs, u->fav_group) : 0;
}

const fav_entry *ui_fav_row(const ui_state *u, int row)
{
    if (u->fav_recents)
        return u->recents && row >= 0 && row < u->recents->count ? &u->recents->e[row] : NULL;
    int i = u->favs ? fav_nth_in_group(u->favs, u->fav_group, row) : -1;
    return i >= 0 ? &u->favs->e[i] : NULL;
}

void ui_favs_changed(ui_state *u)
{
    int rows = ui_fav_rows(u);
    if (u->fl_sel >= rows)
        u->fl_sel = rows ? rows - 1 : 0;
    if (u->fl_top > u->fl_sel)
        u->fl_top = u->fl_sel;
}

static ui_action input_favs(ui_state *u, int in)
{
    switch (in) {
    case IN_UP:
    case IN_DOWN: u->sel = !u->sel; break;
    case IN_BACK: ui_close(u); break;
    case IN_OK:
        u->screen = UI_FAV_LIST;
        u->fav_recents = 0;
        u->fav_group = u->sel ? FAV_SIDS : FAV_GAMES;
        u->fl_sel = u->fl_top = 0;
        break;
    }
    return none();
}

static ui_action input_fav_list(ui_state *u, int in)
{
    int rows = ui_fav_rows(u);
    const fav_entry *e = ui_fav_row(u, u->fl_sel);
    switch (in) {
    case IN_UP:   move_in(&u->fl_sel, &u->fl_top, rows, UI_PAGE, -1, 1); break;
    case IN_DOWN: move_in(&u->fl_sel, &u->fl_top, rows, UI_PAGE, 1, 1); break;
    case IN_L1:   move_in(&u->fl_sel, &u->fl_top, rows, UI_PAGE, -UI_PAGE, 0); break;
    case IN_R1:   move_in(&u->fl_sel, &u->fl_top, rows, UI_PAGE, UI_PAGE, 0); break;
    case IN_BACK:
        if (u->fav_recents)
            ui_close(u);
        else
            go(u, UI_FAVS), u->sel = u->fav_group == FAV_SIDS;
        break;
    case IN_OK:
        if (e) {
            ui_action a = act(UA_FAV_LAUNCH);
            a.fav = *e;
            return a;
        }
        break;
    case IN_TRIANGLE:
        if (e)
            return fav_toggle_action(*e);
        break;
    case IN_ALT:
        if (e && ((fav_is_path(e) && !fav_is_folder(e)) || e->kind == FAV_A64_FILE)) {
            u->nactions = files_actions_for(files_type(e->path, 0), u->actions, FA_COUNT);
            u->action_sel = 0;
            u->action_fav = *e;
            snprintf(u->action_path, sizeof(u->action_path), "%s", e->name);
            u->action_return = UI_FAV_LIST;
            if (u->nactions)
                u->screen = UI_ACTIONS;
        }
        break;
    }
    return none();
}

ui_action ui_input(ui_state *u, int in)
{
    switch (u->screen) {
    case UI_MENU:     return input_menu(u, in);
    case UI_PICKER:   return input_picker(u, in);
    case UI_ADDRESS:  return input_address(u, in);
    case UI_PASSWORD: return input_password(u, in);
    case UI_BROWSER:  return input_browser(u, in);
    case UI_ACTIONS:  return input_actions(u, in);
    case UI_SEARCH:   return input_search(u, in);
    case UI_A64:      return input_a64(u, in);
    case UI_A64_FILES: return input_a64_files(u, in);
    case UI_A64_ID:   return input_a64_id(u, in);
    case UI_FAVS:     return input_favs(u, in);
    case UI_FAV_LIST: return input_fav_list(u, in);
    }
    return none();
}
