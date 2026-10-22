/*
 * sidplay - plays C64 music (.sid files: PSID and RSID) with libsidplayfp's
 * reSIDfp engine, as 16-bit stereo at 48 kHz. A C wrapper around the C++
 * library (sidplay.cpp, built with it into libsidplay.a).
 */
#ifndef SIDPLAY_H
#define SIDPLAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIDPLAY_RATE 48000

typedef struct sidplay sidplay;

enum { SIDPLAY_MODEL_UNKNOWN, SIDPLAY_MODEL_6581, SIDPLAY_MODEL_8580, SIDPLAY_MODEL_ANY };

typedef struct {
    char title[64], author[64], released[64];
    int  songs, start_song, song;   /* 1-based */
    int  model;                     /* SIDPLAY_MODEL_*, of the first SID */
    int  ntsc;                      /* the tune wants an NTSC C64 */
    int  sids;                      /* SID chips the tune uses (1-3) */
    int  length_ms;                 /* the current song's length (song-length database), -1 unknown */
    char md5[33];                   /* the tune's HVSC MD5 (the song-length key) */
    char format[48];                /* e.g. "PlaySID one-file format (PSID)" */
} sidplay_info;

/* C64 ROMs for tunes that need them (RSID, BASIC tunes): KERNAL and BASIC 8 KB, CHARGEN
 * 4 KB, any NULL. Copied; used by players opened afterwards. Most tunes play without. */
void sidplay_set_roms(const uint8_t *kernal, const uint8_t *basic, const uint8_t *chargen);

/* Open HVSC's Songlengths.md5 (once; 0 or -1). Lengths are -1 until it's open. */
int sidplay_songlengths(const char *path);

/* A tune from a .sid file's bytes, ready at its start song; NULL with a reason in err. */
sidplay *sidplay_open(const void *data, size_t len, char *err, size_t cap);
void     sidplay_close(sidplay *p);

/* Switch to song 1..songs (0: the start song) and restart it. 0, or -1. */
int  sidplay_select(sidplay *p, int song);
void sidplay_get_info(const sidplay *p, sidplay_info *info);

/* The next frames of sound (interleaved stereo). Returns the frames written, -1 on error. */
int  sidplay_render(sidplay *p, int16_t *stereo, int frames);
/* Played time of the current song, in ms. */
uint32_t sidplay_time_ms(const sidplay *p);

/* SID chip n's 32 registers as last written (for the visualiser). 0, or -1. */
int  sidplay_regs(sidplay *p, int n, uint8_t regs[32]);

#ifdef __cplusplus
}
#endif

#endif
