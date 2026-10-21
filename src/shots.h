/*
 * shots - screenshots of CSDb releases (http://csdb.dk), fetched and decoded
 * on a worker thread so the picture beside the Assembly64 results never
 * holds up the screen or the Assembly64 requests. For CSDb-sourced items an
 * Assembly64 id is the CSDb release id (see a64_is_csdb).
 */
#ifndef SHOTS_H
#define SHOTS_H

#include <stddef.h>
#include <stdint.h>

typedef struct shots shots;

shots *shots_start(void);
void   shots_stop(shots *s);

/* The screenshot wanted now (a CSDb release id); a newer call replaces an older one. */
void shots_want(shots *s, const char *id);

/*
 * The screenshot for id: 1 with malloc'd copies (free() both) of its frames, an
 * ARGB8888 strip w x h*frames, and each frame's delay in ms; 0 while it's still
 * being fetched; -1 if there is none (or it couldn't be fetched).
 */
int shots_fetch(shots *s, const char *id, uint32_t **pixels, int *w, int *h,
                int *frames, int **delays);

/* Path of a CSDb release's screenshot: 265049 -> /gfx/releases/265000/265049.gif.
 * Returns 0, or -1 if id isn't a release number. */
int shots_url(const char *id, char *out, size_t cap);

#endif
