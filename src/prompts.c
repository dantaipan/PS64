#include "prompts.h"

#include "prompts_svg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* nanosvg's implementation is compiled in home.c; only its declarations here. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "nanosvg.h"
#include "nanosvgrast.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

static const char *const tokens[PROMPT_COUNT] = {
    "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "options", "dpad", "lr",
    "ud", "touch",
};

/* "{token}" at text: its kind, or PROMPT_TEXT. */
static int token_at(const char *text, size_t *len)
{
    if (text[0] != '{')
        return PROMPT_TEXT;
    const char *end = strchr(text + 1, '}');
    if (!end)
        return PROMPT_TEXT;
    size_t n = (size_t)(end - text - 1);
    for (int k = 0; k < PROMPT_COUNT; k++)
        if (strlen(tokens[k]) == n && !strncmp(text + 1, tokens[k], n)) {
            *len = n + 2;
            return k;
        }
    return PROMPT_TEXT;
}

size_t prompts_next(const char *text, int *kind)
{
    *kind = PROMPT_TEXT;
    if (!text || !*text)
        return 0;
    size_t len;
    int k = token_at(text, &len);
    if (k != PROMPT_TEXT) {
        *kind = k;
        return len;
    }
    /* Text up to the next token (a "{" that isn't one stays text). */
    size_t n = 1;
    while (text[n] && token_at(text + n, &len) == PROMPT_TEXT)
        n++;
    return n;
}

int prompts_rasterize(int kind, uint32_t *px, int size)
{
    if (kind < 0 || kind >= PROMPT_COUNT)
        return -1;
    char *copy = strdup(prompt_svgs[kind]);               /* nsvgParse writes into its input */
    if (!copy)
        return -1;
    NSVGimage *img = nsvgParse(copy, "px", 96);
    free(copy);
    if (!img || img->width <= 0) {
        nsvgDelete(img);
        return -1;
    }
    NSVGrasterizer *r = nsvgCreateRasterizer();
    unsigned char *rgba = malloc((size_t)size * size * 4);
    int rc = -1;
    if (r && rgba) {
        nsvgRasterize(r, img, 0, 0, size / img->width, rgba, size, size, size * 4);
        for (int i = 0; i < size * size; i++) {
            const unsigned char *p = rgba + i * 4;
            px[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
        }
        rc = 0;
    }
    free(rgba);
    nsvgDeleteRasterizer(r);
    nsvgDelete(img);
    return rc;
}
