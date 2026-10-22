/*
 * Drives the real VICE core through emulator.c: boots to BASIC, types,
 * reads the joystick from BASIC, starts a PRG, starts an .m3u of two disk
 * images (built here) and swaps to the second, then switches off and on.
 * Checks the C64's screen memory after each step.
 */
#include "../src/c64files.h"
#include "../src/c64keys.h"
#include "../src/c64remote.h"
#include "../src/emugames.h"
#include "../src/emulator.h"
#include "../src/localfs.h"

#include "libretro.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int failures;
static char dir[] = "/tmp/ps64-emu-test-XXXXXX";

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                              printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Screen RAM ($0400) as text, rows joined with '|'. */
static void screen(char *out, size_t cap)
{
    const uint8_t *ram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    size_t n = 0;
    out[0] = 0;
    for (int r = 0; r < 25 && ram; r++) {
        char line[41];
        int len = 0;
        for (int c = 0; c < 40; c++) {
            uint8_t b = ram[0x0400 + r * 40 + c] & 0x7f;
            line[c] = b == 0 ? '@' : b <= 26 ? (char)(b + 64) : (b >= 32 && b < 64) ? (char)b : '.';
            if (line[c] != ' ') len = c + 1;
        }
        line[len] = 0;
        if (len && n + len + 2 < cap)
            n += (size_t)snprintf(out + n, cap - n, "%s|", line);
    }
}

/* Run frames until the screen shows text (or max frames pass); returns frames run. */
static int run_until(const char *text, int max)
{
    char s[1200];
    for (int f = 0; f < max; f++) {
        emulator_run_frame();
        int16_t sink[4096];
        while (emulator_take_audio(sink, 2048))
            ;
        if (f % 10 == 9) {
            screen(s, sizeof(s));
            if (text && strstr(s, text))
                return f + 1;
        }
    }
    return -1;
}

static int count_of(const char *text)
{
    char s[1200];
    screen(s, sizeof(s));
    int n = 0;
    for (const char *p = s; (p = strstr(p, text)) != NULL; p++)
        n++;
    return n;
}

/* Run until text appears once more than it does now. */
static int run_until_another(const char *text, int max)
{
    int before = count_of(text);
    for (int f = 0; f < max; f++) {
        emulator_run_frame();
        int16_t sink[4096];
        while (emulator_take_audio(sink, 2048))
            ;
        if (count_of(text) > before)
            return f + 1;
    }
    return -1;
}

static void type(const char *text)
{
    for (const char *p = text; *p; p++) {
        int key, mods;
        if (c64key_from_ascii((unsigned char)*p, &key, &mods) == 0)
            emulator_key_tap(key, mods);
    }
}

static void write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    fwrite(data, 1, len, f);
    fclose(f);
}

/* BASIC: 10 PRINT"<text>" as a PRG at $0801. */
static size_t basic_print(const char *text, uint8_t *out)
{
    uint8_t line[64];
    size_t n = 0;
    line[n++] = 10; line[n++] = 0; line[n++] = 0x99; line[n++] = '"';
    for (const char *p = text; *p; p++)
        line[n++] = (uint8_t)*p;
    line[n++] = '"'; line[n++] = 0;
    unsigned next = 0x0801 + 2 + (unsigned)n;
    size_t m = 0;
    out[m++] = 0x01; out[m++] = 0x08;
    out[m++] = next & 0xFF; out[m++] = next >> 8;
    memcpy(out + m, line, n);
    m += n;
    out[m++] = 0; out[m++] = 0;
    return m;
}

/* A 35-track D64 holding one PRG named "GAME". */
static void write_d64(const char *path, const char *disk_name, const uint8_t *prg, size_t len)
{
    static uint8_t img[174848];
    memset(img, 0, sizeof(img));
    #define TS(t, s) ((((t) <= 17 ? ((t) - 1) * 21 : (t) <= 24 ? 357 + ((t) - 18) * 19 : \
                       (t) <= 30 ? 490 + ((t) - 25) * 18 : 598 + ((t) - 31) * 17) + (s)) * 256)
    uint8_t *bam = img + TS(18, 0), *dirs = img + TS(18, 1), *data = img + TS(1, 0);
    bam[0] = 18; bam[1] = 1; bam[2] = 0x41;
    for (int t = 1; t <= 35; t++) {
        int n = t <= 17 ? 21 : t <= 24 ? 19 : t <= 30 ? 18 : 17;
        uint32_t bits = (1u << n) - 1;
        if (t == 1) bits &= ~1u;
        if (t == 18) bits &= ~3u;
        uint8_t *e = bam + 4 * t;
        e[0] = (uint8_t)__builtin_popcount(bits);
        e[1] = bits & 0xFF; e[2] = (bits >> 8) & 0xFF; e[3] = (bits >> 16) & 0xFF;
    }
    memset(bam + 0x90, 0xA0, 27);
    memcpy(bam + 0x90, disk_name, strlen(disk_name));
    bam[0xA2] = 'P'; bam[0xA3] = 'S'; bam[0xA5] = '2'; bam[0xA6] = 'A';
    dirs[0] = 0; dirs[1] = 0xFF;
    uint8_t *de = dirs + 2;
    de[0] = 0x82; de[1] = 1; de[2] = 0;
    memset(de + 3, 0xA0, 16);
    memcpy(de + 3, "GAME", 4);
    de[28] = 1;
    data[0] = 0; data[1] = (uint8_t)(len + 1);
    memcpy(data + 2, prg, len);
    write_file(path, img, sizeof(img));
}

int main(void)
{
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 1;
    }
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    char s[1200], path[600];

    CHECK(emulator_start(dir, 2) == 0, "emulator_start");
    /* Loading straight after starting (PS64 does that for --emu-load). */
    uint8_t first[64];
    size_t fn = basic_print("FIRST OK", first);
    snprintf(path, sizeof(path), "%s/first.prg", dir);
    write_file(path, first, fn);
    CHECK(emulator_load(path) == 0, "load right after start");
    CHECK(run_until("FIRST OK", 600) > 0, "the first load runs");
    emulator_reset(1);
    CHECK(run_until("READY.", 400) > 0, "boots to READY.");
    int w = 0, h = 0;
    CHECK(emulator_frame(&w, &h) != NULL && w == 384 && h == 272, "frame %dx%d", w, h);

    /* Keys go into the keyboard matrix. */
    type("PRINT 6*7\n");
    CHECK(run_until(" 42", 300) > 0, "typed PRINT 6*7 shows 42");

    /* Joystick on port 2 ($DC00), read by BASIC. */
    type("10 PRINT PEEK(56320)AND31:GOTO10\nRUN\n");
    CHECK(run_until(" 31", 600) > 0, "joystick idle reads 31");
    emulator_joystick(2, JOY_UP | JOY_FIRE);
    run_until(NULL, 20);
    screen(s, sizeof(s));
    CHECK(strstr(s, " 14|") != NULL, "up+fire reads 14: %s", s);
    emulator_joystick(2, 0);
    emulator_key_tap(C64K_RUN_STOP, 0);
    CHECK(run_until("BREAK", 100) > 0, "RUN/STOP breaks the program");

    /* A PRG autostarts. */
    uint8_t prg[64];
    size_t n = basic_print("PRG OK", prg);
    snprintf(path, sizeof(path), "%s/test.prg", dir);
    write_file(path, prg, n);
    CHECK(emulator_load(path) == 0, "load prg");
    CHECK(run_until("PRG OK", 600) > 0, "PRG runs");

    /* Two disks in an .m3u: the first autostarts, then swap to the second. */
    char d1[600], d2[600], m3u[600];
    snprintf(d1, sizeof(d1), "%s/Disk1.d64", dir);
    snprintf(d2, sizeof(d2), "%s/Disk2.d64", dir);
    n = basic_print("DISK 1 OK", prg);
    write_d64(d1, "DISK ONE", prg, n);
    n = basic_print("DISK 2 OK", prg);
    write_d64(d2, "DISK TWO", prg, n);
    snprintf(m3u, sizeof(m3u), "%s/game.m3u", dir);
    FILE *f = fopen(m3u, "w");
    fprintf(f, "%s\n%s\n", d1, d2);
    fclose(f);
    CHECK(emulator_load(m3u) == 0, "load m3u");
    int fast_seen = 0, frames = 0;
    for (; frames < 4000; frames++) {
        emulator_run_frame();
        int16_t sink[4096];
        while (emulator_take_audio(sink, 2048))
            ;
        fast_seen |= emulator_fast();
        screen(s, sizeof(s));
        if (strstr(s, "DISK 1 OK"))
            break;
    }
    CHECK(frames < 4000, "disk 1 autostarts: %s", s);
    CHECK(fast_seen, "fast loading was requested while the disk loaded");
    CHECK(emulator_disk_count() == 2, "2 disks, got %d", emulator_disk_count());
    CHECK(emulator_disk_index() == 0, "disk 1 inserted");
    CHECK(emulator_disk_insert(1) == 0 && emulator_disk_index() == 1, "swap to disk 2");
    run_until(NULL, 30);
    type("LOAD\"*\",8\n");
    CHECK(run_until_another("LOADING|READY.|", 4000) > 0, "disk 2 loads");
    type("RUN\n");                      /* the C64 doesn't read keys while loading */
    int got = run_until("DISK 2 OK", 300);
    screen(s, sizeof(s));
    CHECK(got > 0, "disk 2 loads after the swap: %s", s);

    /* Insert a disk without starting it. */
    CHECK(emulator_insert_disk(d1) == 0, "insert disk");

    /* The games folder: a disk starts with the other disks of its folder. */
    files_set_emulator(1);
    char games[600], cache[600], file[700];
    snprintf(games, sizeof(games), "%s/games", dir);
    snprintf(cache, sizeof(cache), "%s/emulator", dir);
    snprintf(file, sizeof(file), "%s/Multi/Disk1.d64", games);
    localfs_mkdirs(file); rmdir(file);                     /* make the folder */
    n = basic_print("DISK 1 OK", prg);
    write_d64(file, "DISK ONE", prg, n);
    snprintf(file, sizeof(file), "%s/Multi/Disk2.d64", games);
    n = basic_print("DISK 2 OK", prg);
    write_d64(file, "DISK TWO", prg, n);
    snprintf(file, sizeof(file), "%s/Music/tune.sid", games);
    localfs_write(file, "PSID", 4);
    emugames g;
    eg_init(&g, games, cache);
    char msg[200];
    emulator_reset(1);
    CHECK(eg_launch_path(&g, "/Multi/Disk2.d64", FA_NONE, msg, sizeof(msg)) == EG_STARTED,
          "start Disk2: %s", msg);
    CHECK(strstr(msg, "disk 1 of 2") != NULL, "message: %s", msg);
    CHECK(run_until("DISK 2 OK", 4000) > 0, "Disk2 chosen, so Disk2 starts");
    CHECK(emulator_disk_count() == 2, "both disks to swap, got %d", emulator_disk_count());
    CHECK(eg_launch_path(&g, "/Multi", FA_LAUNCH_FOLDER, msg, sizeof(msg)) == EG_STARTED, "folder: %s", msg);
    CHECK(run_until("DISK 1 OK", 4000) > 0, "a folder starts its first disk");
    CHECK(eg_launch_path(&g, "/Music/tune.sid", FA_NONE, msg, sizeof(msg)) == EG_ERROR &&
          strstr(msg, "C64 Ultimate"), "SID refused: %s", msg);
    CHECK(eg_launch_path(&g, "/Music", FA_LAUNCH_FOLDER, msg, sizeof(msg)) == EG_ERROR, "nothing to start");
    CHECK(eg_launch_path(&g, "/Multi/Disk2.d64", FA_MOUNT_A, msg, sizeof(msg)) == EG_STARTED &&
          strstr(msg, "drive 8"), "insert: %s", msg);

    /* Assembly64: the chosen disk plus the item's other disks are fetched, then started. */
    a64_file files[3] = { { 11, "Disk1.d64", 0 }, { 12, "Disk2.d64", 0 }, { 13, "readme.txt", 0 } };
    a64 *net = a64_start("http://127.0.0.1:9/leet");   /* downloads go nowhere; we feed them */
    CHECK(eg_a64_begin(&g, net, "item-1", 0, "Test Game", files, 3, 0, FA_NONE, msg, sizeof(msg)) ==
          EG_WAITING && strstr(msg, "1 of 2"), "waits for the downloads: %s", msg);
    static uint8_t img[174848];
    for (int d = 1; d <= 2; d++) {
        char text[16];
        snprintf(text, sizeof(text), "A64 D%d OK", d);
        n = basic_print(text, prg);
        snprintf(file, sizeof(file), "%s/dl.d64", dir);
        write_d64(file, "A64", prg, n);
        FILE *df = fopen(file, "rb");
        size_t len = fread(img, 1, sizeof(img), df);
        fclose(df);
        a64_result r;
        memset(&r, 0, sizeof(r));
        r.kind = A64R_FILE;
        r.data = img;
        r.len = len;
        snprintf(r.name, sizeof(r.name), "Disk%d.d64", d);
        int rc = eg_a64_file(&g, net, &r, msg, sizeof(msg));
        CHECK(rc == (d == 1 ? EG_WAITING : EG_STARTED), "download %d: %d %s", d, rc, msg);
    }
    CHECK(run_until("A64 D1 OK", 4000) > 0, "Assembly64 disk 1 starts");
    CHECK(emulator_disk_count() == 2, "Assembly64 disks to swap");
    /* Started again: everything is cached now, nothing to download. */
    CHECK(eg_a64_begin(&g, net, "item-1", 0, "Test Game", files, 3, 1, FA_NONE, msg, sizeof(msg)) ==
          EG_STARTED, "cached start: %s", msg);
    CHECK(run_until("A64 D2 OK", 4000) > 0, "cached Disk2 starts");
    a64_stop(net);
    files_set_emulator(0);

    /* Off and on: back to an empty BASIC. */
    emulator_reset(1);
    CHECK(run_until("READY.", 400) > 0, "power cycle boots to READY.");
    screen(s, sizeof(s));
    CHECK(!strstr(s, "OK"), "screen cleared after power cycle: %s", s);
    CHECK(emulator_disk_count() == 0, "disks detached");

    clock_gettime(CLOCK_MONOTONIC, &t1);
    emulator_stop();
    printf("test_emulator: %s (%.1f s)\n", failures ? "FAILED" : "all passed",
           (double)(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9);
    return failures ? 1 : 0;
}
