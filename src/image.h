/*
 * image - decode a GIF or PNG held in memory (CSDb screenshots) to ARGB8888,
 * using stb_image (src/stb_image.h, public domain / MIT).
 */
#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define IMAGE_MAX_FRAMES 8      /* frames kept from an animated GIF (418 KB each at 384x272) */

/*
 * Returns 0 and the image's frames as one malloc'd ARGB8888 strip, w x h*frames (frame i
 * starts at row h*i), plus a malloc'd delay in ms per frame (free() both), or -1. A PNG or
 * a still GIF is one frame. CSDb screenshots with several pictures are animated GIFs.
 */
int image_decode(const void *data, size_t len, uint32_t **argb, int *w, int *h,
                 int *frames, int **delays_ms);

#endif
