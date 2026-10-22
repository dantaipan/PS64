#include "home.h"

#include "font8x8.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "nanosvg.h"
#include "nanosvgrast.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#define C64_BORDER 0xFF706DEBu
#define C64_SCREEN 0xFF2E2C9Bu
#define C64_TEXT   0xFF706DEBu

static void fill(uint32_t *px, int w, int x0, int y0, int x1, int y1, uint32_t argb)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            px[y * w + x] = argb;
}

void home_text(uint32_t *px, int w, int h, int x, int y, const char *text, int scale, uint32_t argb)
{
    for (const unsigned char *p = (const unsigned char *)text; *p; p++, x += 8 * scale) {
        if (*p < FONT_FIRST || *p > FONT_LAST)
            continue;
        const unsigned char *g = font8x8[*p - FONT_FIRST];
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++) {
                if (!((g[row] >> col) & 1))
                    continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int px_x = x + col * scale + sx, px_y = y + row * scale + sy;
                        if (px_x >= 0 && px_x < w && px_y >= 0 && px_y < h)
                            px[px_y * w + px_x] = argb;
                    }
            }
    }
}

void home_basic_screen(uint32_t *px, int cursor)
{
    const int w = HOME_C64_W, h = HOME_C64_H, x0 = HOME_C64_X, y0 = HOME_C64_Y;
    fill(px, w, 0, 0, w, h, C64_BORDER);
    fill(px, w, x0, y0, x0 + 320, y0 + 200, C64_SCREEN);
    /* Rows 1, 3 and 5 of the 40x25 text screen, as a C64 starts up. */
    home_text(px, w, h, x0, y0 + 8, "    **** COMMODORE 64 BASIC V2 ****", 1, C64_TEXT);
    home_text(px, w, h, x0, y0 + 24, " 64K RAM SYSTEM  38911 BASIC BYTES FREE", 1, C64_TEXT);
    home_text(px, w, h, x0, y0 + 40, "READY.", 1, C64_TEXT);
    if (cursor)
        fill(px, w, x0, y0 + 48, x0 + 8, y0 + 56, C64_TEXT);
}

void home_noise(uint32_t *px, int n, uint32_t *seed)
{
    uint32_t s = *seed ? *seed : 0x9E3779B9u;
    for (int i = 0; i < n; i++) {
        s ^= s << 13;                                   /* xorshift32 */
        s ^= s >> 17;
        s ^= s << 5;
        uint32_t v = 40 + (s & 0xFF) * 180 / 255;       /* not quite black or white */
        px[i] = 0xFF000000u | v << 16 | v << 8 | v;
    }
    *seed = s;
}

void home_chip_label(uint32_t *px)
{
    const int w = HOME_LABEL_W, h = HOME_LABEL_H;
    const uint32_t body = 0xFF26262A, ink = 0xFFB4B4B8, dark = 0xFF141416;
    fill(px, w, 0, 0, w, h, body);
    /* The notch at pin 1's end, and the dot by pin 1. */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < 14; x++) {
            int dy = y - h / 2;
            if (x * x + dy * dy < 13 * 13)
                px[y * w + x] = dark;
        }
    for (int y = h - 18; y < h - 8; y++)
        for (int x = 20; x < 30; x++) {
            int dx = x - 25, dy = y - (h - 13);
            if (dx * dx + dy * dy <= 20)
                px[y * w + x] = dark;
        }
    home_text(px, w, h, 44, 10, "MOS", 2, ink);
    home_text(px, w, h, 44, 32, "6581R4", 2, ink);
    home_text(px, w, h, 44, 56, "1285 25", 1, ink);
}

int home_icon(uint32_t *px, int size, const char *svg)
{
    char *copy = strdup(svg);                       /* nsvgParse writes into its input */
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
        for (int i = 0; i < size * size; i++)
            px[i] = (uint32_t)rgba[i * 4 + 3] << 24 | 0xFFFFFF;
        rc = 0;
    }
    free(rgba);
    nsvgDeleteRasterizer(r);
    nsvgDelete(img);
    return rc;
}

void home_disc(uint32_t *px, int size)
{
    float c = (size - 1) / 2.0f, r = size / 2.0f - 1;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float d = sqrtf((x - c) * (x - c) + (y - c) * (y - c));
            float a = r - d + 0.5f;                     /* one pixel of soft edge */
            a = a < 0 ? 0 : a > 1 ? 1 : a;
            px[y * size + x] = (uint32_t)(a * 255) << 24 | 0xFFFFFF;
        }
}
