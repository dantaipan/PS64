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

#define HOME_LABEL_W 240    /* the chip's top */
#define HOME_LABEL_H 80

/* Text with the 8x8 font into a w x h buffer at x, y, each pixel scale x scale. */
void home_text(uint32_t *px, int w, int h, int x, int y, const char *text, int scale, uint32_t argb);

/* The BASIC start-up screen (HOME_C64_W x HOME_C64_H), with the cursor on or off. */
void home_basic_screen(uint32_t *px, int cursor);

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
