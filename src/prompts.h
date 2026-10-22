/*
 * prompts - controller button icons inside PS64's hint lines. A hint is
 * written with tokens, "{cross} play  {l1}{r1} page", and drawn with the
 * button icons (Kenney's Input Prompts, src/prompts_svg.h) in place of the
 * tokens. Only PS64's own hint strings use tokens: names from files or
 * Assembly64 are never drawn this way. Splitting and rasterising are here
 * (pure); drawing is prompts_sdl.c.
 */
#ifndef PROMPTS_H
#define PROMPTS_H

#include <stddef.h>
#include <stdint.h>

enum { PROMPT_CROSS, PROMPT_CIRCLE, PROMPT_SQUARE, PROMPT_TRIANGLE, PROMPT_L1, PROMPT_R1,
       PROMPT_L2, PROMPT_R2, PROMPT_OPTIONS, PROMPT_DPAD, PROMPT_LR, PROMPT_UD, PROMPT_TOUCH,
       PROMPT_COUNT };

#define PROMPT_TEXT (-1)

/*
 * The next run of text: returns its length in bytes and sets *kind to a PROMPT_* icon
 * (the run is its "{token}") or PROMPT_TEXT. 0 at the end of the string.
 */
size_t prompts_next(const char *text, int *kind);

/* Icon kind as ARGB8888 in a size x size buffer (its own colours). 0, or -1. */
int prompts_rasterize(int kind, uint32_t *px, int size);

#endif
