/*
 * image - decode a GIF or PNG held in memory (CSDb screenshots) to ARGB8888,
 * using stb_image (src/stb_image.h, public domain / MIT).
 */
#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdint.h>

/* Returns 0 and a malloc'd w*h ARGB8888 image (free() it), or -1. */
int image_decode(const void *data, size_t len, uint32_t **argb, int *w, int *h);

#endif
