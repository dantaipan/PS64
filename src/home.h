/*
 * home - pictures for the home screen's objects, made in pixel buffers
 * (ARGB8888; pure, tested): the C64's start-up screen, TV static, and the
 * SID chip's printed top. Drawing the screen itself is home_sdl.c.
 */
#ifndef HOME_H
#define HOME_H

#include <stdint.h>

/* A C64 screen as the VIC-II shows it: 320x200 inside a border. */
#define HOME_C64_W 384
#define HOME_C64_H 272
#define HOME_C64_X 32       /* top left of the 40x25 text area */
#define HOME_C64_Y 36

/* A VIC-20 screen: 22x23 characters (176x184) inside a border; shown stretched onto a 4:3
 * screen, which gives it the VIC-20's wide pixels. */
#define HOME_VIC20_W 224
#define HOME_VIC20_H 232
#define HOME_VIC20_X 24     /* top left of the text area */
#define HOME_VIC20_Y 24

#define HOME_LABEL_W 240    /* the chip's top */
#define HOME_LABEL_H 80

/* Text with the 8x8 font into a w x h buffer at x, y, each pixel scale x scale. */
void home_text(uint32_t *px, int w, int h, int x, int y, const char *text, int scale, uint32_t argb);

/* The BASIC start-up screen (HOME_C64_W x HOME_C64_H), with the cursor on or off. */
void home_basic_screen(uint32_t *px, int cursor);

/* The monitors' printed labels (DECAL_W x DECAL_H, rectangles in models.h): the 1702's
 * badge, model plate and captions; the 1084's "1084" box, logo and power button. */
void home_1702_decals(uint32_t *px);
void home_1084_decals(uint32_t *px);

/* The VIC-20's start-up screen (HOME_VIC20_W x HOME_VIC20_H), with the cursor on or off. */
void home_vic20_screen(uint32_t *px, int cursor);

/* The carousel: offset d (in objects) wrapped round n objects into (-n/2, n/2], and how
 * much of an object at offset off is shown: whole up to its neighbours' places (|off| <= 1),
 * shrinking to nothing at 2. */
float home_wrap(float d, int n);
float home_shown(float off);

/*
 * An old TV's RF reception of picture base (w x h) at time t_ms, into out: always a little
 * line wobble, a ghost to the right, colour bleed and snow; now and then (home_rf_burst) a
 * short burst of tearing, more snow and a dark band rolling down. *seed carries the snow's
 * random state.
 */
void home_rf(const uint32_t *base, uint32_t *out, int w, int h, unsigned t_ms, uint32_t *seed);
/* Whether a burst is on at t_ms: 1 with *progress 0..1 through it, else 0. A burst of
 * HOME_RF_BURST_MS starts every 6 to 9 seconds. */
#define HOME_RF_BURST_MS 600
int  home_rf_burst(unsigned t_ms, float *progress);

/* n pixels of grey TV static; *seed carries the random state between calls. */
void home_noise(uint32_t *px, int n, uint32_t *seed);

/* The SID chip's top (HOME_LABEL_W x HOME_LABEL_H): maker, part number, date code. */
void home_chip_label(uint32_t *px);

/* An SVG drawn into a size x size buffer (anti-aliased), as white with the shape's
 * coverage as alpha. Returns 0, or -1 if the SVG couldn't be read. */
int home_icon(uint32_t *px, int size, const char *svg);

/* A soft-edged white disc filling a size x size buffer (alpha only, for highlights). */
void home_disc(uint32_t *px, int size);

#endif
