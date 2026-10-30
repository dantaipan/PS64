#include "home.h"

#include "font8x8.h"
#include "models.h"

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

/* The Commodore logo in a size x size square at x, y: the C (a ring open to the right)
 * and the flag's two bars. */
static void cbm_logo(uint32_t *px, int w, int x, int y, int size, uint32_t argb)
{
    float r = size / 2.0f, cx = x + r, cy = y + r;
    for (int j = 0; j < size; j++)
        for (int i = 0; i < size; i++) {
            float dx = i + 0.5f - r, dy = j + 0.5f - r, d = sqrtf(dx * dx + dy * dy);
            int ring = d <= r && d >= r * 0.52f && !(dx > 0 && fabsf(dy) < dx * 0.75f);
            int flag = dx >= r * 0.08f && dx <= r * 0.95f &&
                       ((dy >= -r * 0.46f && dy <= -r * 0.08f) || (dy >= r * 0.08f && dy <= r * 0.46f &&
                                                                    dx <= r * 0.08f + (r * 0.46f - dy)));
            if (ring || flag)
                px[(int)(cy - r + j) * w + (int)(cx - r + i)] = argb;
        }
}

static void outline(uint32_t *px, int w, int x0, int y0, int x1, int y1, uint32_t argb)
{
    fill(px, w, x0, y0, x1, y0 + 1, argb);
    fill(px, w, x0, y1 - 1, x1, y1, argb);
    fill(px, w, x0, y0, x0 + 1, y1, argb);
    fill(px, w, x1 - 1, y0, x1, y1, argb);
}

/* The 1702's case colour: the captions sit on it. */
#define C1702_CASE  0xFFB4A68Cu
#define C1084_CASE  0xFFE0DCD1u

void home_1702_decals(uint32_t *px)
{
    const int w = DECAL_W;
    fill(px, w, 0, 0, w, DECAL_H, C1702_CASE);
    /* The badge: brown (darker than the case, lighter than the bezel), the logo and
     * "commodore" in silver, the rainbow, the dark POWER window with its light. */
    const decal_rect b = DECAL_1702_BADGE;
    fill(px, w, b.x, b.y, b.x + b.w, b.y + b.h, 0xFF7E6C58);
    cbm_logo(px, w, b.x + 5, b.y + 4, 16, 0xFFD2D4D8);
    home_text(px, w, DECAL_H, b.x + 24, b.y + 8, "commodore", 1, 0xFFD2D4D8);
    static const uint32_t rainbow[5] = { 0xFFE03A2F, 0xFFF08A24, 0xFFF5D02E, 0xFF3FA34D, 0xFF3B7DD8 };
    for (int i = 0; i < 5; i++)
        fill(px, w, b.x + 100, b.y + 3 + i * 4, b.x + 128, b.y + 6 + i * 4, rainbow[i]);
    fill(px, w, b.x + 134, b.y, b.x + b.w, b.y + b.h, 0xFF3A3A3C);
    fill(px, w, b.x + 145, b.y + 5, b.x + 150, b.y + 7, 0xFFD03A2E);       /* the LED */
    /* The model plate: printed on the case, in a thin outline. */
    const decal_rect mp = DECAL_1702_MODEL;
    outline(px, w, mp.x, mp.y, mp.x + mp.w, mp.y + mp.h, 0xFF5A5248);
    home_text(px, w, DECAL_H, mp.x + 4, mp.y + 3, "VIDEO MONITOR", 1, 0xFF3A342E);
    home_text(px, w, DECAL_H, mp.x + 4, mp.y + 13, "MODEL 1702", 1, 0xFF3A342E);
}

void home_1084_decals(uint32_t *px)
{
    const int w = DECAL_W;
    const uint32_t blue = 0xFF2A4F9Eu, ink = 0xFF55585E;
    fill(px, w, 0, 0, w, DECAL_H, C1084_CASE);
    /* "1084" in its thin box. */
    const decal_rect l = DECAL_1084_LABEL;
    outline(px, w, l.x + 1, l.y + 2, l.x + l.w - 1, l.y + l.h - 2, 0xFF9A968C);
    home_text(px, w, DECAL_H, l.x + (l.w - 32) / 2, l.y + 6, "1084", 1, blue);
    /* The logo. */
    const decal_rect g = DECAL_1084_LOGO;
    cbm_logo(px, w, g.x + 4, g.y + 2, 12, blue);
    home_text(px, w, DECAL_H, g.x + 20, g.y + 4, "Commodore", 1, blue);
    /* The power button: POWER and the power sign (a ring open at the top, a bar into it). */
    const decal_rect p = DECAL_1084_POWER;
    fill(px, w, p.x, p.y, p.x + p.w, p.y + p.h, 0xFFE8E4DA);
    home_text(px, w, DECAL_H, p.x, p.y + 1, "POWER", 1, ink);
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++) {
            float dx = i - 3.5f, dy = j - 3.5f, d = sqrtf(dx * dx + dy * dy);
            if ((d <= 4 && d >= 2.6f && !(dy < 0 && fabsf(dx) < 1.6f)) || (fabsf(dx) < 0.8f && dy < 0.5f))
                px[(p.y + 11 + j) * w + p.x + 16 + i] = ink;
        }
}

#define VIC20_BORDER 0xFF78D1FFu        /* (VICE's VIC-20 colours) */
#define VIC20_SCREEN 0xFFFFFFFFu
#define VIC20_TEXT   0xFF4F2EA9u

void home_vic20_screen(uint32_t *px, int cursor)
{
    const int w = HOME_VIC20_W, h = HOME_VIC20_H, x0 = HOME_VIC20_X, y0 = HOME_VIC20_Y;
    fill(px, w, 0, 0, w, h, VIC20_BORDER);
    fill(px, w, x0, y0, x0 + 176, y0 + 184, VIC20_SCREEN);
    home_text(px, w, h, x0, y0, "**** CBM BASIC V2 ****", 1, VIC20_TEXT);
    home_text(px, w, h, x0, y0 + 8, "3583 BYTES FREE", 1, VIC20_TEXT);
    home_text(px, w, h, x0, y0 + 24, "READY.", 1, VIC20_TEXT);
    if (cursor)
        fill(px, w, x0, y0 + 32, x0 + 8, y0 + 40, VIC20_TEXT);
}

float home_wrap(float d, int n)
{
    while (d > n / 2.0f)
        d -= (float)n;
    while (d <= -n / 2.0f)
        d += (float)n;
    return d;
}

float home_shown(float off)
{
    float a = off < 0 ? -off : off;
    return a <= 1 ? 1 : a >= 2 ? 0 : 2 - a;
}

/* A small integer hash (bursts and tears are placed by it, so they repeat for a time). */
static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

int home_rf_burst(unsigned t_ms, float *progress)
{
    /* One burst somewhere in each 7.5 s, so 6 to 9 s apart. */
    unsigned cycle = t_ms / 7500, start = cycle * 7500 + hash32(cycle) % 1500;
    if (t_ms < start || t_ms >= start + HOME_RF_BURST_MS)
        return 0;
    *progress = (float)(t_ms - start) / HOME_RF_BURST_MS;
    return 1;
}

static uint32_t mix_argb(uint32_t a, uint32_t b, int wb)       /* b weighted wb/256 */
{
    uint32_t r = (((a >> 16) & 255) * (256 - wb) + ((b >> 16) & 255) * wb) >> 8;
    uint32_t g = (((a >> 8) & 255) * (256 - wb) + ((b >> 8) & 255) * wb) >> 8;
    uint32_t bl = ((a & 255) * (256 - wb) + (b & 255) * wb) >> 8;
    return 0xFF000000u | r << 16 | g << 8 | bl;
}

void home_rf(const uint32_t *base, uint32_t *out, int w, int h, unsigned t_ms, uint32_t *seed)
{
    uint32_t s = *seed ? *seed : 0x2545F491u;
    float burst = 0;
    int bursting = home_rf_burst(t_ms, &burst);
    float strength = bursting ? sinf(3.14159265f * burst) : 0;    /* rises and falls */
    int band_y = (int)((burst * 1.3f - 0.15f) * h), band_h = h / 8;
    unsigned snow = bursting ? 46 : 16;                            /* of 256: ~6%, ~18% */
    for (int y = 0; y < h; y++) {
        /* The line's shift: a slow wobble, a little jitter, and during a burst whole groups
         * of lines torn sideways. */
        float wob = sinf(y * 0.15f + t_ms * 0.004f) * 0.8f;
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        int shift = (int)lrintf(wob) + ((s & 15) == 0 ? ((s >> 4) & 1 ? 1 : -1) : 0);
        if (bursting) {
            uint32_t tear = hash32((uint32_t)(y / 6) * 2654435761u ^ (t_ms / 80));
            shift += (int)lrintf(((int)(tear % 17) - 8) * strength);
        }
        int dark = bursting && y >= band_y && y < band_y + band_h;
        const uint32_t *row = base + y * w;
        uint32_t left = row[0];
        for (int x = 0; x < w; x++) {
            int sx = x - shift, gx = sx - 6;
            uint32_t c = row[sx < 0 ? 0 : sx >= w ? w - 1 : sx];
            uint32_t g = row[gx < 0 ? 0 : gx >= w ? w - 1 : gx];
            c = mix_argb(c, g, 46);                    /* the ghost, ~18% */
            c = mix_argb(c, left, 90);                 /* colour bleed from the left */
            left = c;
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            if ((s & 255) < snow) {
                uint32_t v = 90 + ((s >> 8) & 127);
                c = mix_argb(c, 0xFF000000u | v << 16 | v << 8 | v, 150);
            }
            if (dark)
                c = mix_argb(c, 0xFF000000u, 110);
            out[y * w + x] = c;
        }
    }
    *seed = s;
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
