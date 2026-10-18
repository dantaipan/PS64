/* Unit tests for keyboard/joystick forwarding logic: make test */
#include "../src/a64.h"
#include "../src/c64api.h"
#include "../src/c64files.h"
#include "../src/c64index.h"
#include "../src/c64keys.h"
#include "../src/c64remote.h"
#include "../src/discover.h"
#include "../src/favs.h"
#include "../src/ftp.h"
#include "../src/osk.h"
#include "../src/settings.h"
#include "../src/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void test_keys(void)
{
    /* PETSCII from the KERNAL tables. */
    CHECK(c64key_petscii(10, 0) == 'A');                       /* A */
    CHECK(c64key_petscii(10, C64MOD_SHIFT) == 0xC1);           /* shifted A */
    CHECK(c64key_petscii(C64K_RETURN, 0) == 0x0D);
    CHECK(c64key_petscii(C64K_RUN_STOP, 0) == 0x03);
    CHECK(c64key_petscii(C64K_F1, 0) == 0x85 && c64key_petscii(C64K_F1, C64MOD_SHIFT) == 0x89);
    CHECK(c64key_petscii(C64K_F7, C64MOD_SHIFT) == 0x8C);      /* F8 */
    CHECK(c64key_petscii(C64K_CLR_HOME, 0) == 0x13 && c64key_petscii(C64K_CLR_HOME, C64MOD_SHIFT) == 0x93);
    CHECK(c64key_petscii(C64K_INST_DEL, 0) == 0x14 && c64key_petscii(C64K_INST_DEL, C64MOD_SHIFT) == 0x94);
    CHECK(c64key_petscii(C64K_CRSR_UD, C64MOD_SHIFT) == 0x91);  /* cursor up */
    CHECK(c64key_petscii(C64K_CRSR_LR, C64MOD_SHIFT) == 0x9D);  /* cursor left */
    CHECK(c64key_petscii(56, C64MOD_CTRL) == 0x90);            /* CTRL-1 black */
    CHECK(c64key_petscii(56, C64MOD_CBM) == 0x81);             /* C=-1 orange */
    CHECK(c64key_petscii(C64K_LSHIFT, 0) == -1);               /* modifiers don't type */
    CHECK(c64key_petscii(C64K_RESTORE, 0) == -1);
    CHECK(c64key_petscii(C64K_F1, C64MOD_CTRL) == -1);         /* no CTRL code */

    /* REST names with modifiers. */
    const char *names[4];
    CHECK(c64key_api_inputs(C64K_F1, C64MOD_SHIFT, names) == 2 &&
          !strcmp(names[0], "left_shift") && !strcmp(names[1], "f1"));
    CHECK(c64key_api_inputs(C64K_RESTORE, C64MOD_SHIFT, names) == 1 && !strcmp(names[0], "restore"));
    CHECK(c64key_api_inputs(C64K_CBM, 0, names) == 0);
    CHECK(c64key_api_inputs(56, C64MOD_CTRL | C64MOD_CBM, names) == 3 &&
          !strcmp(names[0], "commodore") && !strcmp(names[1], "ctrl") && !strcmp(names[2], "1"));

    /* Config names. */
    int key, mods;
    CHECK(c64key_parse("f8", &key, &mods) == 0 && key == C64K_F7 && mods == C64MOD_SHIFT);
    CHECK(c64key_parse("RUN_STOP", &key, &mods) == 0 && key == C64K_RUN_STOP && mods == 0);
    CHECK(c64key_parse("clr", &key, &mods) == 0 && key == C64K_CLR_HOME && mods == C64MOD_SHIFT);
    CHECK(c64key_parse("cursor_left", &key, &mods) == 0 && key == C64K_CRSR_LR && mods == C64MOD_SHIFT);
    CHECK(c64key_parse("q", &key, &mods) == 0 && key == 62);
    CHECK(c64key_parse("bogus", &key, &mods) == -1);

    /* Labels follow shift. */
    CHECK(!strcmp(c64key_label(C64K_F5, C64MOD_SHIFT), "F6"));
    CHECK(!strcmp(c64key_label(C64K_INST_DEL, 0), "DEL"));
}

static void test_json(void)
{
    remote_event ev[3];
    memset(ev, 0, sizeof(ev));
    ev[0].kind = EV_JOYSTICK; ev[0].port = 2; ev[0].transition = TR_PRESS;
    ev[0].count = 2; ev[0].names[0] = "up"; ev[0].names[1] = "fire";
    ev[1].kind = EV_KEYBOARD; ev[1].transition = TR_TAP;
    ev[1].count = 2; ev[1].names[0] = "left_shift"; ev[1].names[1] = "a";
    ev[2].kind = EV_RELEASE_ALL;
    char out[512];
    size_t n = remote_events_json(out, sizeof(out), ev, 3);
    const char *want =
        "{\"events\":["
        "{\"kind\":\"joystick\",\"port\":2,\"inputs\":[\"up\",\"fire\"],\"transition\":\"press\"},"
        "{\"kind\":\"keyboard\",\"inputs\":[\"left_shift\",\"a\"],\"transition\":\"tap\"},"
        "{\"kind\":\"release_all\"}]}";
    CHECK(n == strlen(want) && !strcmp(out, want));
    CHECK(remote_events_json(out, 20, ev, 3) == 0);             /* doesn't fit */

    /* The largest batch the sender builds stays inside the 4096-byte limit:
     * release_all + REMOTE_MAX_KEY_EVENTS worst-case keys + two joystick events. */
    remote_event many[REMOTE_MAX_KEY_EVENTS + 3];
    int nm = 0;
    memset(many, 0, sizeof(many));
    many[nm++].kind = EV_RELEASE_ALL;
    for (int i = 0; i < REMOTE_MAX_KEY_EVENTS; i++, nm++) {
        many[nm].kind = EV_KEYBOARD; many[nm].transition = TR_TAP;
        many[nm].count = 4;
        many[nm].names[0] = "left_shift"; many[nm].names[1] = "commodore";
        many[nm].names[2] = "ctrl"; many[nm].names[3] = "cursor_left_right";
    }
    for (int i = 0; i < 2; i++, nm++) {
        many[nm].kind = EV_JOYSTICK; many[nm].port = 2;
        many[nm].transition = i ? TR_PRESS : TR_RELEASE; many[nm].count = 5;
        many[nm].names[0] = "up"; many[nm].names[1] = "down"; many[nm].names[2] = "left";
        many[nm].names[3] = "right"; many[nm].names[4] = "fire";
    }
    static char big[4096];
    size_t len = remote_events_json(big, sizeof(big), many, nm);
    printf("largest batch: %zu bytes\n", len);
    CHECK(len > 0 && len < 4096);
}

static void test_joystick(void)
{
    joy_track t;
    memset(&t, 0, sizeof(t));
    t.port = 2;
    remote_event ev[3];

    /* Press up+fire: one press event. */
    int n = joy_plan(&t, 2, JOY_UP | JOY_FIRE, 1000, ev);
    CHECK(n == 1 && ev[0].transition == TR_PRESS && ev[0].count == 2 && ev[0].port == 2);
    CHECK(t.sent == (JOY_UP | JOY_FIRE));

    /* Releasing fire 20 ms later is held back until 60 ms have passed. */
    n = joy_plan(&t, 2, JOY_UP, 1020, ev);
    CHECK(n == 0 && t.sent == (JOY_UP | JOY_FIRE));
    n = joy_plan(&t, 2, JOY_UP, 1060, ev);
    CHECK(n == 1 && ev[0].transition == TR_RELEASE && ev[0].count == 1 &&
          !strcmp(ev[0].names[0], "fire"));
    CHECK(t.sent == JOY_UP);

    /* Nothing changes: nothing sent. */
    CHECK(joy_plan(&t, 2, JOY_UP, 1100, ev) == 0);

    /* Release and press in one pass touch different inputs only. */
    n = joy_plan(&t, 2, JOY_DOWN, 1200, ev);
    CHECK(n == 2 && ev[0].transition == TR_RELEASE && !strcmp(ev[0].names[0], "up") &&
          ev[1].transition == TR_PRESS && !strcmp(ev[1].names[0], "down"));

    /* A port switch releases everything on the old port first. */
    n = joy_plan(&t, 1, JOY_DOWN, 1300, ev);
    CHECK(n == 1 && ev[0].port == 2 && ev[0].transition == TR_RELEASE && t.sent == 0);
    n = joy_plan(&t, 1, JOY_DOWN, 1310, ev);
    CHECK(n == 1 && ev[0].port == 1 && ev[0].transition == TR_PRESS);
}

static void test_osk(void)
{
    /* Every C64 key appears exactly once on the on-screen keyboard. */
    int seen[C64K_COUNT] = {0};
    for (int r = 0; r < OSK_ROWS; r++)
        for (int c = 0; c < osk_row_len(r); c++) {
            const osk_key *k = osk_key_at(r, c);
            CHECK(k->x + k->w <= OSK_WIDTH);
            seen[k->key]++;
        }
    for (int k = 0; k < C64K_COUNT; k++)
        if (seen[k] != 1) {
            printf("key %d (%s) appears %d times\n", k, c64key_api_name(k), seen[k]);
            failures++;
        }

    osk_state s;
    osk_init(&s);
    CHECK(osk_key_at(s.row, s.col)->key == 10);                 /* starts on A */
    osk_move(&s, 0, -1);                                         /* up: Q or W, above A */
    int above = osk_key_at(s.row, s.col)->key;
    CHECK(above == 62 || above == 9);
    osk_move(&s, -1, 0);
    osk_move(&s, -1, 0);
    osk_move(&s, -1, 0);                                         /* wraps to F3 at row end */
    CHECK(osk_key_at(s.row, s.col)->key == C64K_F3 || osk_key_at(s.row, s.col)->key == C64K_RESTORE);
    s.row = 4; s.col = 0;
    osk_move(&s, 0, 1);                                          /* space -> wraps to top row */
    CHECK(s.row == 0);

    /* One-shot shift applies once; locked shift stays. */
    int key, mods;
    osk_init(&s);
    osk_toggle_shift(&s);
    CHECK(osk_press(&s, &key, &mods) == 1 && key == 10 && mods == C64MOD_SHIFT);
    CHECK(osk_press(&s, &key, &mods) == 1 && mods == 0);
    osk_toggle_shift(&s);
    osk_toggle_shift(&s);                                        /* locked */
    osk_press(&s, &key, &mods);
    CHECK(osk_press(&s, &key, &mods) == 1 && mods == C64MOD_SHIFT);

    /* Pressing the on-screen SHIFT key toggles instead of typing. */
    osk_init(&s);
    s.row = 3; s.col = 1;
    CHECK(osk_key_at(s.row, s.col)->key == C64K_LSHIFT);
    CHECK(osk_press(&s, &key, &mods) == 0 && s.shift == 1);
}

static void test_labels(void)
{
    /* What-you-type labels follow the modifiers. */
    CHECK(!strcmp(c64key_label(59, C64MOD_SHIFT), "\""));      /* 2 -> " */
    CHECK(!strcmp(c64key_label(56, C64MOD_SHIFT), "!"));       /* 1 -> ! */
    CHECK(!strcmp(c64key_label(45, C64MOD_SHIFT), "["));       /* : -> [ */
    CHECK(!strcmp(c64key_label(48, 0), "\x80"));               /* pound */
    CHECK((unsigned char)c64key_label(10, C64MOD_SHIFT)[0] == 0xC1);   /* shift A: spade */
    CHECK((unsigned char)c64key_label(10, C64MOD_CBM)[0] == 0xB0);     /* C= A */
    CHECK(!strcmp(c64key_label(56, C64MOD_CTRL), "BLK"));
    CHECK(!strcmp(c64key_label(27, C64MOD_CBM), "GRY3"));      /* C= 8 */
    CHECK(!strcmp(c64key_label(32, C64MOD_CTRL), "RVS+"));     /* CTRL 9 */
    CHECK(!strcmp(c64key_label(C64K_F1, C64MOD_SHIFT), "F2"));
    CHECK(!strcmp(c64key_label(C64K_RUN_STOP, C64MOD_SHIFT), "LOAD"));
    CHECK(!strcmp(c64key_label(C64K_SPACE, C64MOD_SHIFT), "SPACE"));
    CHECK(!strcmp(c64key_label(10, 0), "A"));

    int key, mods;
    CHECK(c64key_from_ascii('"', &key, &mods) == 0 && key == 59 && mods == C64MOD_SHIFT);
    CHECK(c64key_from_ascii('l', &key, &mods) == 0 && key == 42 && mods == 0);
    CHECK(c64key_from_ascii('\n', &key, &mods) == 0 && key == C64K_RETURN);
}

static const char *slurp(const char *path)
{
    static char buf[4096];
    FILE *f = fopen(path, "r");
    size_t n = f ? fread(buf, 1, sizeof(buf) - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = 0;
    return buf;
}

static void test_settings(void)
{
    char path[] = "/tmp/ps64_test_cfgXXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    const char *orig =
        "# PS64 settings\n"
        "start = 192.168.2.64\n"
        "# c64 = 192.168.1.64\n"
        "audio-latency = 60\n";
    CHECK(write(fd, orig, strlen(orig)) == (ssize_t)strlen(orig));
    close(fd);

    static const char *const legacy[] = { "start", NULL };
    CHECK(config_set(path, "c64", "10.0.0.5", legacy) == 0);
    CHECK(!strcmp(slurp(path),
        "# PS64 settings\n"
        "# start = 192.168.2.64\n"
        "c64 = 10.0.0.5\n"
        "audio-latency = 60\n"));
    CHECK(config_set(path, "audio-latency", "80", NULL) == 0);    /* replace */
    CHECK(config_set(path, "browse-dir", "/SD/games/A", NULL) == 0);  /* append */
    CHECK(!strcmp(slurp(path),
        "# PS64 settings\n"
        "# start = 192.168.2.64\n"
        "c64 = 10.0.0.5\n"
        "audio-latency = 80\n"
        "browse-dir = /SD/games/A\n"));
    remove(path);
}

static void test_discover_and_api(void)
{
    const char *body = "{ \n  \"product\" : \"C64 Ultimate\",\n  \"firmware_version\" : \"1.1.0\",\n"
                       "  \"fpga_version\" : \"122\",\n  \"hostname\" : \"C64-Ultimate-5F3538\",\n"
                       "  \"errors\" : [  ]\n}";
    c64_device d;
    memset(&d, 0, sizeof(d));
    CHECK(discover_parse_info(body, &d) == 0);
    CHECK(!strcmp(d.product, "C64 Ultimate") && !strcmp(d.firmware, "1.1.0") &&
          !strcmp(d.hostname, "C64-Ultimate-5F3538"));
    CHECK(discover_parse_info("{ \"product\" : \"Router\" }", &d) == -1);
    CHECK(discover_parse_info("{}", &d) == -1);

    char enc[256];
    c64api_url_encode("/SD/games/_recent_releases/2026/Bricks 'n Balls v1.0.d64", enc, sizeof(enc));
    CHECK(!strcmp(enc, "/SD/games/_recent_releases/2026/Bricks%20%27n%20Balls%20v1.0.d64"));
    c64api_url_encode("/SD/a+b&c.prg", enc, sizeof(enc));
    CHECK(!strcmp(enc, "/SD/a%2Bb%26c.prg"));
}

static void test_ftp_and_files(void)
{
    ftp_entry e;
    CHECK(ftp_parse_list_line("-rw-rw-rw-   1 user     ftp      3321081 Nov 19  2020 Ace of Aces Manual.cbr", &e) == 0);
    CHECK(!strcmp(e.name, "Ace of Aces Manual.cbr") && e.size == 3321081 && !e.is_dir);
    CHECK(ftp_parse_list_line("drw-rw-rw-   1 user     ftp            0 Jan 01  1980 SD", &e) == 0);
    CHECK(!strcmp(e.name, "SD") && e.is_dir);
    CHECK(ftp_parse_list_line("drw-rw-rw-   1 user     ftp            0 Feb 10 18:23 1-9", &e) == 0);
    CHECK(!strcmp(e.name, "1-9"));
    CHECK(ftp_parse_list_line("total 4", &e) == -1);
    CHECK(ftp_parse_pasv_port("227 Entering Passive Mode (192,168,2,64,195,80)") == 195 * 256 + 80);

    ftp_entry list[4];
    memset(list, 0, sizeof(list));
    strcpy(list[0].name, "zeta.prg");
    strcpy(list[1].name, "Alpha");   list[1].is_dir = 1;
    strcpy(list[2].name, "beta.d64");
    strcpy(list[3].name, "_CRT");    list[3].is_dir = 1;
    ftp_sort(list, 4);
    CHECK(!strcmp(list[0].name, "_CRT") && !strcmp(list[1].name, "Alpha") &&
          !strcmp(list[2].name, "beta.d64") && !strcmp(list[3].name, "zeta.prg"));

    CHECK(files_type("game.PRG", 0) == FT_PRG && files_type("disk.d64", 0) == FT_DISK);
    CHECK(files_type("x.g64", 0) == FT_DISK && files_type("a.crt", 0) == FT_CRT);
    CHECK(files_type("tune.sid", 0) == FT_SID && files_type("readme.txt", 0) == FT_OTHER);
    CHECK(files_type("folder.d64", 1) == FT_DIR);
    CHECK(files_default_action(FT_DISK) == FA_MOUNT_RUN && files_default_action(FT_PRG) == FA_RUN);
    CHECK(files_default_action(FT_OTHER) == FA_NONE);
    int acts[FA_COUNT];
    CHECK(files_actions_for(FT_DISK, acts, FA_COUNT) == 3 && acts[0] == FA_MOUNT_RUN);

    unsigned char screen[1000];
    memset(screen, 0x20, sizeof(screen));
    CHECK(!files_screen_ready(screen, 1000));
    memcpy(screen + 200, "\x12\x05\x01\x04\x19\x2e", 6);         /* READY. in screen codes */
    CHECK(files_screen_ready(screen, 1000));
}

static void test_ui(void)
{
    static ui_state u;
    ui_init(&u);

    /* Menu wraps; Options-style close. */
    ui_open_menu(&u);
    ui_input(&u, IN_UP);
    CHECK(u.sel == MI_QUIT);
    CHECK(ui_input(&u, IN_OK).kind == UA_QUIT);
    u.sel = MI_AUDIO;
    CHECK(ui_input(&u, IN_RIGHT).kind == UA_MUTE);
    u.sel = MI_RESET;
    CHECK(ui_input(&u, IN_OK).kind == UA_RESET && u.screen == UI_CLOSED);
    ui_open_menu(&u);
    u.sel = MI_REBOOT;
    CHECK(ui_input(&u, IN_OK).kind == UA_REBOOT && u.screen == UI_CLOSED);
    ui_open_menu(&u);
    u.sel = MI_CHOOSE;
    CHECK(ui_input(&u, IN_OK).kind == UA_RESCAN && u.screen == UI_PICKER);

    /* Picker: a found device connects; the fixed rows follow it. */
    c64_device dev[1];
    memset(dev, 0, sizeof(dev));
    strcpy(dev[0].ip, "192.168.2.64");
    ui_set_devices(&u, dev, 1, 100, 1);
    CHECK(ui_picker_rows(&u) == 4);
    ui_action a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_CONNECT && !strcmp(a.ip, "192.168.2.64") && u.screen == UI_CLOSED);

    /* Address entry: pre-filled from the device, octets wrap. */
    ui_open_picker(&u, NULL);
    u.sel = 1;                                                   /* Enter address... */
    ui_input(&u, IN_OK);
    CHECK(u.screen == UI_ADDRESS && u.octets[0] == 192 && u.octets[3] == 64 && u.field == 3);
    ui_input(&u, IN_R1); ui_input(&u, IN_R1);                    /* 84 */
    ui_input(&u, IN_UP);                                         /* 85 */
    CHECK(u.octets[3] == 85);
    u.octets[3] = 255;
    ui_input(&u, IN_UP);
    CHECK(u.octets[3] == 0);
    ui_input(&u, IN_DOWN);
    CHECK(u.octets[3] == 255);
    ui_input(&u, IN_LEFT);
    CHECK(u.field == 2);
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_CONNECT && !strcmp(a.ip, "192.168.2.255"));

    /* Locked device asks for a password typed on the on-screen keyboard. */
    dev[0].locked = 1;
    ui_open_picker(&u, NULL);
    ui_set_devices(&u, dev, 1, 100, 1);
    ui_input(&u, IN_OK);
    CHECK(u.screen == UI_PASSWORD);
    ui_input(&u, IN_OK);                                         /* types 'a' (starts on A) */
    osk_toggle_shift(&u.kbd);
    ui_input(&u, IN_OK);                                         /* 'A' */
    CHECK(!strcmp(u.password, "aA"));
    ui_input(&u, IN_ALT);                                        /* delete */
    a = ui_input(&u, IN_R2);
    CHECK(a.kind == UA_CONNECT && !strcmp(a.password, "a"));

    /* Browser: open folder, run file, go up keeps the folder selected. */
    a = ui_open_browser(&u, "/SD/games");
    CHECK(a.kind == UA_LIST && !strcmp(a.path, "/SD/games") && u.loading);
    ftp_entry *ents = calloc(3, sizeof(ftp_entry));
    strcpy(ents[0].name, "A"); ents[0].is_dir = 1;
    strcpy(ents[1].name, "B"); ents[1].is_dir = 1;
    strcpy(ents[2].name, "Bricks 'n Balls.d64");
    ui_set_listing(&u, "/SD/games", ents, 3);
    ui_input(&u, IN_DOWN);
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_LIST && !strcmp(a.path, "/SD/games/B"));
    ui_set_listing(&u, "/SD/games/B", calloc(1, sizeof(ftp_entry)), 0);
    a = ui_input(&u, IN_BACK);
    CHECK(a.kind == UA_LIST && !strcmp(a.path, "/SD/games"));
    ents = calloc(3, sizeof(ftp_entry));
    strcpy(ents[0].name, "A"); ents[0].is_dir = 1;
    strcpy(ents[1].name, "B"); ents[1].is_dir = 1;
    strcpy(ents[2].name, "Bricks 'n Balls.d64");
    ui_set_listing(&u, "/SD/games", ents, 3);
    CHECK(u.sel == 1);                                           /* back on "B" */
    ui_input(&u, IN_DOWN);
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_FILE && a.file_action == FA_MOUNT_RUN &&
          !strcmp(a.path, "/SD/games/Bricks 'n Balls.d64"));
    ui_input(&u, IN_ALT);                                        /* action list */
    CHECK(u.screen == UI_ACTIONS && u.nactions == 3);
    ui_input(&u, IN_DOWN);
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_FILE && a.file_action == FA_MOUNT_A && u.screen == UI_BROWSER);

    char parent[64];
    ui_parent("/SD", parent, sizeof(parent));
    CHECK(!strcmp(parent, "/"));
    ui_parent("/SD/games/A/", parent, sizeof(parent));
    CHECK(!strcmp(parent, "/SD/games"));
    ui_free(&u);
}

static void test_index(void)
{
    c64index *x = index_new();
    index_add(x, "/SD/games/S/Space Ace/Space Ace.d64", 174848);
    index_add(x, "/SD/games/A/Ace of Aces/Ace of Aces.crt", 147808);
    index_add(x, "/SD/games/B/Boulder Dash/Boulder Dash.prg", 20000);
    index_add(x, "/SD/games/_recent_releases/2026/Bricks 'n Balls v1.0.d64", 174848);
    index_add(x, "/SD/games/B/Boulder Dash/Disk 2/side b.d64", 174848);
    index_add(x, "/SD/sids/Hubbard/Commando.sid", 5000);

    index_hit hits[10];
    /* File-name whole-word match ranks first; folder-only match last. */
    int n = index_search(x, "ace", hits, 10);
    CHECK(n == 2);
    CHECK(n == 2 && !strcmp(hits[0].path, "/SD/games/A/Ace of Aces/Ace of Aces.crt"));
    CHECK(n == 2 && hits[0].type == FT_CRT && hits[1].type == FT_DISK);
    /* Every word must match somewhere in the path, any case. */
    n = index_search(x, "BOULDER disk", hits, 10);
    CHECK(n == 1 && !strcmp(hits[0].path, "/SD/games/B/Boulder Dash/Disk 2/side b.d64"));
    n = index_search(x, "boulder", hits, 10);
    CHECK(n == 2 && !strcmp(hits[0].path, "/SD/games/B/Boulder Dash/Boulder Dash.prg"));
    CHECK(index_search(x, "bricks 'n", hits, 10) == 1);
    index_add(x, "/SD/games/B/Boulder Dash II/Boulder Dash II.d64", 174848);
    n = index_search(x, "boulder dash", hits, 10);
    CHECK(n == 3 && !strcmp(hits[0].path, "/SD/games/B/Boulder Dash/Boulder Dash.prg") &&
          !strcmp(hits[1].path, "/SD/games/B/Boulder Dash II/Boulder Dash II.d64"));
    CHECK(index_search(x, "nothing here", hits, 10) == 0);
    CHECK(index_search(x, "   ", hits, 10) == 0);
    CHECK(index_search(x, "sd", hits, 3) == 3);                 /* capped */

    /* Save and load round trip, tied to the host. */
    char path[] = "/tmp/ps64_test_idxXXXXXX";
    int fd = mkstemp(path);
    close(fd);
    CHECK(index_save(x, path, "192.168.2.64", "/SD") == 0);
    c64index *y = index_new();
    CHECK(index_load(y, path, "10.0.0.1") == -1);              /* other C64 */
    CHECK(index_load(y, path, "192.168.2.64") == 0 && index_count(y) == 7);
    n = index_search(y, "bricks", hits, 10);
    CHECK(n == 1 && !strcmp(hits[0].path, "/SD/games/_recent_releases/2026/Bricks 'n Balls v1.0.d64") &&
          hits[0].size == 174848);
    remove(path);

    /* Search screen: typing filters, R2 moves to results, Cross runs. */
    static ui_state u;
    ui_init(&u);
    u.index = y;
    ui_open_search(&u);
    CHECK(u.screen == UI_SEARCH && u.search_focus == 0);
    ui_text(&u, "commando");
    CHECK(u.nresults == 1);
    ui_action a = ui_input(&u, IN_R2);
    CHECK(u.search_focus == 1);
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_FILE && a.file_action == FA_PLAY && !strcmp(a.path, "/SD/sids/Hubbard/Commando.sid"));
    ui_input(&u, IN_BACK);                                       /* back to typing */
    CHECK(u.search_focus == 0);
    for (int i = 0; i < 8; i++)
        ui_input(&u, IN_ALT);                                    /* delete the query */
    CHECK(u.query[0] == 0 && u.nresults == 0);
    ui_text(&u, "boulder");
    ui_input(&u, IN_R2);
    ui_input(&u, IN_ALT);                                        /* action list */
    CHECK(u.screen == UI_ACTIONS && u.action_return == UI_SEARCH && u.nactions == 2);
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_FILE && a.file_action == FA_RUN && u.screen == UI_SEARCH);
    a = ui_input(&u, IN_RIGHT);                                  /* show in folder */
    CHECK(a.kind == UA_LIST && !strcmp(a.path, "/SD/games/B/Boulder Dash") && u.screen == UI_BROWSER);

    /* Empty index: Cross asks main to build one. */
    c64index *empty = index_new();
    u.index = empty;
    ui_open_search(&u);
    CHECK(ui_input(&u, IN_OK).kind == UA_BUILD_INDEX);
    ui_free(&u);
    index_free(empty);
    index_free(y);
    index_free(x);
}

static void test_a64(void)
{
    char q[256];
    a64_build_query("boulder dash", "games", q, sizeof(q));
    CHECK(!strcmp(q, "(name:\"boulder dash\") & (category:games)"));
    a64_build_query("say \"hi\"", NULL, q, sizeof(q));
    CHECK(!strcmp(q, "(name:\"say \\\"hi\\\"\")"));

    char host[64], base[64];
    int port;
    CHECK(c64api_parse_url("http://hackerswithstyle.se/leet", host, sizeof(host), &port, base, sizeof(base)) == 0 &&
          !strcmp(host, "hackerswithstyle.se") && port == 80 && !strcmp(base, "/leet"));
    CHECK(c64api_parse_url("http://127.0.0.1:8081/leet/", host, sizeof(host), &port, base, sizeof(base)) == 0 &&
          !strcmp(host, "127.0.0.1") && port == 8081 && !strcmp(base, "/leet"));
    CHECK(c64api_parse_url("https://x/leet", host, sizeof(host), &port, base, sizeof(base)) == -1);

    /* Shapes from the Assembly64 OpenAPI spec. */
    const char *items =
        "[{\"name\":\"Boulder Dash\",\"id\":\"abc123\",\"category\":2,\"siteRating\":8.5,"
        "\"group\":\"First Star\",\"year\":1984,\"rating\":7,\"updated\":\"2020-01-01\"},"
        "{\"name\":\"Boulder \\\"Dash\\\" II\",\"id\":\"def\",\"category\":2,\"group\":\"\","
        "\"handle\":\"Someone\",\"year\":0},"
        "{\"name\":\"no id\"}]";
    a64_item *it = NULL;
    int n = a64_parse_items(items, strlen(items), &it);
    CHECK(n == 2);
    CHECK(n == 2 && !strcmp(it[0].name, "Boulder Dash") && !strcmp(it[0].id, "abc123") &&
          it[0].category == 2 && it[0].year == 1984 && !strcmp(it[0].group, "First Star"));
    CHECK(n == 2 && !strcmp(it[1].name, "Boulder \"Dash\" II") && !strcmp(it[1].group, "Someone"));
    free(it);

    const char *entries =
        "{\"contentEntry\":[{\"path\":\"boulder.d64\",\"id\":0,\"size\":174848,\"date\":1774129200000},"
        "{\"path\":\"readme.txt\",\"id\":1,\"size\":120}]}";
    a64_file *f = NULL;
    n = a64_parse_files(entries, strlen(entries), &f);
    CHECK(n == 2 && !strcmp(f[0].path, "boulder.d64") && f[0].id == 0 && f[0].size == 174848 && f[1].id == 1);
    free(f);

    const char *presets =
        "[{\"type\":\"date\",\"values\":[{\"id\":1,\"aqlKey\":\"1984\",\"name\":\"1984\"}]},"
        "{\"type\":\"category\",\"description\":\"Category\",\"values\":["
        "{\"id\":1,\"aqlKey\":\"games\",\"name\":\"Games\"},{\"id\":2,\"aqlKey\":\"demos\",\"name\":\"Demos\"}]}]";
    a64_categories cats;
    CHECK(a64_parse_categories(presets, strlen(presets), &cats) == 2 &&
          !strcmp(cats.keys[0], "games") && !strcmp(cats.names[1], "Demos"));

    /* Multipart download: first part's body. */
    const char *mp = "--XyZ\r\nContent-Type: application/octet-stream\r\n\r\nDATA\x01\x02\r\n--XyZ--\r\n";
    unsigned char *d = (unsigned char *)strdup(mp);
    size_t len = strlen(mp);
    a64_unwrap_multipart("multipart/mixed; boundary=XyZ", &d, &len);
    CHECK(len == 6 && !memcmp(d, "DATA\x01\x02", 6));
    size_t before = len;
    a64_unwrap_multipart("application/octet-stream", &d, &len);   /* untouched */
    CHECK(len == before);
    free(d);

    /* Screens: no ID -> ID entry; save; search; open item; run a file. */
    static ui_state u;
    ui_init(&u);
    ui_open_menu(&u);
    u.sel = MI_A64;
    ui_input(&u, IN_OK);
    CHECK(u.screen == UI_A64_ID);
    ui_text(&u, "my-client");
    ui_action a = ui_input(&u, IN_R2);
    CHECK(a.kind == UA_A64_SET_ID && !strcmp(a.text, "my-client") && u.screen == UI_A64);
    u.a64_ready = 1;
    u.a64_cats = cats;
    ui_text(&u, "boulder");
    ui_input(&u, IN_L2);                                          /* category: Games */
    a = ui_input(&u, IN_R2);
    CHECK(a.kind == UA_A64_SEARCH && !strcmp(a.text, "boulder") && !strcmp(a.category_key, "games") &&
          a.offset == 0 && u.a64_loading);

    a64_result r;
    memset(&r, 0, sizeof(r));
    r.kind = A64R_SEARCH;
    r.count = A64_PAGE;                                          /* a full page: more to come */
    r.items = calloc(A64_PAGE, sizeof(a64_item));
    strcpy(r.items[0].id, "abc123");
    r.items[0].category = 2;
    strcpy(r.items[0].name, "Boulder Dash");
    ui_a64_results(&u, &r);
    CHECK(u.a64_count == A64_PAGE && u.a64_more && u.a64_focus == 1 && !u.a64_loading);
    u.a64_sel = A64_PAGE;                                        /* "More results..." */
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_A64_SEARCH && a.offset == A64_PAGE);
    u.a64_loading = 0;
    u.a64_sel = 0;
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_A64_ENTRIES && !strcmp(a.text, "abc123") && a.category == 2 && u.screen == UI_A64_FILES);

    memset(&r, 0, sizeof(r));
    r.kind = A64R_ENTRIES;
    r.nfiles = 1;
    r.files = calloc(1, sizeof(a64_file));
    strcpy(r.files[0].path, "boulder.d64");
    r.files[0].id = 7;
    ui_a64_results(&u, &r);
    u.a64_loading = 0;
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_A64_FILE && !strcmp(a.text, "abc123") && a.category == 2 && a.file_id == 7 &&
          !strcmp(a.path, "boulder.d64") && a.file_action == FA_MOUNT_RUN);
    ui_input(&u, IN_ALT);                                        /* action list */
    CHECK(u.screen == UI_ACTIONS && u.action_return == UI_A64_FILES);
    ui_input(&u, IN_DOWN);
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_A64_FILE && a.file_action == FA_MOUNT_A && u.screen == UI_A64_FILES);

    /* A rejected client ID goes back to ID entry with the message. */
    ui_a64_error(&u, "Assembly64 didn't accept the client ID (HTTP 464)", 1);
    CHECK(u.screen == UI_A64_ID && strstr(u.a64_status, "464"));
    ui_free(&u);
}

static void test_favs(void)
{
    fav_list l;
    fav_init(&l, 0);
    fav_entry game = fav_local("/SD/games/B/Boulder Dash", 1);
    fav_entry disk = fav_local("/SD/games/B/Boulder Dash/Disk1.d64", 0);
    fav_entry sid = fav_local("/SD/sids/HVSC/MUSICIANS/H/Hubbard_Rob/Commando.sid", 0);
    CHECK(game.kind == FAV_LOCAL_FOLDER && game.group == FAV_GAMES && !strcmp(game.name, "Boulder Dash"));
    CHECK(disk.kind == FAV_LOCAL_FILE && !strcmp(disk.name, "Boulder Dash/Disk1.d64"));
    CHECK(sid.group == FAV_SIDS && !strcmp(sid.name, "Hubbard_Rob/Commando.sid"));

    CHECK(fav_toggle(&l, &game) == 1 && fav_toggle(&l, &sid) == 1 && l.count == 2);
    CHECK(fav_find(&l, &disk) == -1);                    /* folder and its disk are different */
    CHECK(fav_count_group(&l, FAV_GAMES) == 1 && fav_count_group(&l, FAV_SIDS) == 1);
    CHECK(fav_nth_in_group(&l, FAV_SIDS, 0) == 1 && fav_nth_in_group(&l, FAV_SIDS, 1) == -1);
    CHECK(fav_toggle(&l, &game) == 0 && l.count == 1);   /* removed */

    fav_entry a1 = fav_a64_file("bd1", 2, 0, "boulder dash.d64", "Boulder Dash");
    fav_entry a2 = fav_a64_file("bd1", 2, 1, "boulder dash side b.d64", "Boulder Dash");
    CHECK(!strcmp(a1.name, "Boulder Dash - boulder dash.d64") && a1.group == FAV_GAMES);
    fav_toggle(&l, &a1);
    CHECK(fav_find(&l, &a1) >= 0 && fav_find(&l, &a2) == -1);   /* file id matters */

    /* Save/load keeps everything, names with apostrophes and spaces included. */
    fav_entry odd = fav_local("/SD/games/_recent_releases/2026/Bricks 'n Balls v1.0.d64", 0);
    fav_toggle(&l, &odd);
    char path[] = "/tmp/ps64_test_favXXXXXX";
    int fd = mkstemp(path);
    close(fd);
    CHECK(fav_save(&l, path) == 0);
    fav_list m;
    fav_init(&m, 0);
    CHECK(fav_load(&m, path) == 0 && m.count == 3);
    CHECK(m.count == 3 && !strcmp(m.e[2].path, "/SD/games/_recent_releases/2026/Bricks 'n Balls v1.0.d64") &&
          m.e[1].kind == FAV_A64_FILE && !strcmp(m.e[1].item_id, "bd1") && m.e[1].category == 2);
    remove(path);
    fav_free(&m);
    fav_free(&l);

    /* Recents: newest first, duplicates move up, capped. */
    fav_list r;
    fav_init(&r, RECENTS_MAX);
    for (int i = 0; i < 30; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/SD/games/X/Game %d/Disk1.d64", i);
        fav_entry e = fav_local(p, 0);
        fav_add_recent(&r, &e);
    }
    CHECK(r.count == RECENTS_MAX && strstr(r.e[0].path, "Game 29/") && strstr(r.e[24].path, "Game 5/"));
    fav_entry again = fav_local("/SD/games/X/Game 10/Disk1.d64", 0);
    fav_add_recent(&r, &again);
    CHECK(r.count == RECENTS_MAX && strstr(r.e[0].path, "Game 10/") && strstr(r.e[1].path, "Game 29/"));
    fav_add_recent(&r, &r.e[3]);                         /* re-adding an entry from the list itself */
    CHECK(r.count == RECENTS_MAX && strstr(r.e[0].path, "Game 27/"));
    fav_free(&r);

    CHECK(fav_group_for_category("Music") == FAV_SIDS && fav_group_for_category("sids") == FAV_SIDS &&
          fav_group_for_category("Games") == FAV_GAMES && fav_group_for_category(NULL) == FAV_GAMES);

    /* Which file of a folder to start. */
    const char *f1[] = { "readme.txt", "Disk2.d64", "Disk1.d64", "loader.prg" };
    CHECK(files_pick_launch(f1, 4) == 2);
    const char *f2[] = { "intro.sid", "game.crt", "game.prg" };
    CHECK(files_pick_launch(f2, 3) == 2);
    const char *f3[] = { "tune.sid", "notes.txt" };
    CHECK(files_pick_launch(f3, 2) == 0);
    const char *f4[] = { "notes.txt" };
    CHECK(files_pick_launch(f4, 1) == -1);

    /* UI: Triangle favourites in the browser, L2 opens search there. */
    static ui_state u;
    ui_init(&u);
    fav_list favs, recents;
    fav_init(&favs, 0);
    fav_init(&recents, RECENTS_MAX);
    u.favs = &favs;
    u.recents = &recents;
    ui_open_browser(&u, "/SD/games/B");
    ftp_entry *ents = calloc(2, sizeof(ftp_entry));
    strcpy(ents[0].name, "Boulder Dash"); ents[0].is_dir = 1;
    strcpy(ents[1].name, "loose.prg");
    ui_set_listing(&u, "/SD/games/B", ents, 2);
    CHECK(ui_input(&u, IN_L2).kind == UA_OPEN_SEARCH);
    ui_action a = ui_input(&u, IN_TRIANGLE);
    CHECK(a.kind == UA_FAV_TOGGLE && a.fav.kind == FAV_LOCAL_FOLDER &&
          !strcmp(a.fav.path, "/SD/games/B/Boulder Dash"));
    fav_toggle(&favs, &a.fav);
    CHECK(ui_is_fav_local(&u, "/SD/games/B/Boulder Dash", 1));

    /* Menu > Favourites > Games > Cross launches; Triangle removes. */
    ui_open_menu(&u);
    u.sel = MI_FAVS;
    ui_input(&u, IN_OK);
    CHECK(u.screen == UI_FAVS);
    ui_input(&u, IN_OK);                                         /* Games */
    CHECK(u.screen == UI_FAV_LIST && !u.fav_recents && ui_fav_rows(&u) == 1);
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_FAV_LAUNCH && a.fav.kind == FAV_LOCAL_FOLDER);
    a = ui_input(&u, IN_TRIANGLE);
    CHECK(a.kind == UA_FAV_TOGGLE && !strcmp(a.fav.path, "/SD/games/B/Boulder Dash"));
    ui_input(&u, IN_BACK);
    CHECK(u.screen == UI_FAVS);

    /* Recents: Square opens actions for a file and returns a launch with that action. */
    fav_entry rd = fav_local("/SD/games/B/Boulder Dash/Disk1.d64", 0);
    fav_add_recent(&recents, &rd);
    ui_open_menu(&u);
    u.sel = MI_RECENTS;
    ui_input(&u, IN_OK);
    CHECK(u.screen == UI_FAV_LIST && u.fav_recents && ui_fav_rows(&u) == 1);
    ui_input(&u, IN_ALT);
    CHECK(u.screen == UI_ACTIONS && u.action_return == UI_FAV_LIST && u.nactions == 3);
    ui_input(&u, IN_DOWN);
    a = ui_input(&u, IN_OK);
    CHECK(a.kind == UA_FAV_LAUNCH && a.fav.action == FA_MOUNT_A && u.screen == UI_FAV_LIST);
    a = ui_input(&u, IN_TRIANGLE);
    CHECK(a.kind == UA_FAV_TOGGLE && a.fav.kind == FAV_LOCAL_FILE);

    /* Assembly64 item: Triangle favourites it, grouped by category name. */
    u.a64_ready = 1;
    u.a64_cats.count = 1;
    strcpy(u.a64_cats.names[0], "Music");
    strcpy(u.a64_cats.keys[0], "music");
    u.a64_cats.ids[0] = 5;
    ui_open_a64(&u);
    u.a64_items = calloc(1, sizeof(a64_item));
    u.a64_count = 1;
    strcpy(u.a64_items[0].id, "m1");
    strcpy(u.a64_items[0].name, "Commando");
    u.a64_items[0].category = 5;
    u.a64_focus = 1;
    a = ui_input(&u, IN_TRIANGLE);
    CHECK(a.kind == UA_FAV_TOGGLE && a.fav.kind == FAV_A64_ITEM && a.fav.group == FAV_SIDS &&
          !strcmp(a.fav.item_id, "m1"));
    ui_free(&u);
    fav_free(&favs);
    fav_free(&recents);
}

int main(void)
{
    test_favs();
    test_a64();
    test_index();
    test_labels();
    test_settings();
    test_discover_and_api();
    test_ftp_and_files();
    test_ui();
    test_keys();
    test_json();
    test_joystick();
    test_osk();
    printf(failures ? "%d test(s) failed\n" : "all input tests passed\n", failures);
    return failures != 0;
}
