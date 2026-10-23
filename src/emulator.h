/*
 * emulator - PS64's built-in C64: the VICE emulator (its libretro core,
 * x64sc, linked statically) run frame by frame from the main loop.
 *
 * There is one emulated C64 per process (the core keeps global state). All
 * calls come from the main thread. Built only with PS64_EMU defined.
 */
#ifndef EMULATOR_H
#define EMULATOR_H

#include <stddef.h>
#include <stdint.h>

#define EMULATOR_MAX_W 384
#define EMULATOR_MAX_H 288

/*
 * Boot the C64 to BASIC. dir holds VICE's working files (created if
 * missing). Returns 0, or -1 if the core wouldn't start. Starting it again
 * switches it off and on (the core can only be set up once per process).
 */
int  emulator_start(const char *dir, int joystick_port);
void emulator_stop(void);
int  emulator_running(void);

/* Emulate one frame (1/50 s of C64 time). */
void   emulator_run_frame(void);
double emulator_fps(void);
int    emulator_sample_rate(void);
/* The core asks to run as fast as possible (a disk or tape is loading). */
int    emulator_fast(void);

/* The latest frame as ARGB8888 (w*h pixels, pitch w), or NULL before the first. */
const uint32_t *emulator_frame(int *w, int *h);
/* Stereo s16 audio produced since the last call; returns frames copied. */
size_t emulator_take_audio(int16_t *out, size_t max_frames);
/* Pops one message from the emulator ("Disk 2 inserted"...), for a banner. */
int    emulator_take_message(char *out, size_t cap);

/*
 * Start a file: PRG, T64/TAP, CRT, a disk image, or an .m3u list of disks
 * (the first is started, the others are there to swap to). 0 or -1.
 */
int  emulator_load(const char *path);
/* Put a disk image into drive 8 without starting anything. */
int  emulator_insert_disk(const char *path);
/*
 * The cartridge kept in the expansion port (a .crt, e.g. a fast loader), or NULL for
 * none: put in straight away (the C64 is switched off and on) and again after each
 * load and power cycle, except when a cartridge game is loaded. Can be set before
 * emulator_start. 0, or -1 if VICE couldn't attach it (then none).
 */
int  emulator_set_cartridge(const char *path);
/* Reset (programs and cartridges stay), or switch off and on (everything detached). */
void emulator_reset(int power_cycle);

/* Keys are c64keys ids (matrix codes and C64K_RESTORE). */
void emulator_key(int key, int down);
/* Press and release, with modifiers held around it (queued, frame-timed). */
void emulator_key_tap(int key, int mods);
/* Release all keys and the joystick, drop queued taps. */
void emulator_release_keys(void);
/* JOY_* bits on port 1 or 2. */
void emulator_joystick(int port, unsigned bits);

/* Disk swapping, for disks loaded from an .m3u list. */
int  emulator_disk_count(void);
int  emulator_disk_index(void);
int  emulator_disk_insert(int index);

#endif
