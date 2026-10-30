#include "prompts_sdl.h"

#include "font_sdl.h"
#include "prompts.h"

#include <stdio.h>
#include <string.h>

#define ICON_PX 48                      /* rasterised size (drawn smaller, smoothly) */

static SDL_Renderer *icons_for;
static SDL_Texture  *icons[PROMPT_COUNT];
static SDL_Rect      drawn[PROMPT_COUNT];   /* the part of each icon with something on it */

static void load_icons(SDL_Renderer *ren)
{
    if (icons_for == ren)
        return;
    prompts_cleanup();
    icons_for = ren;
    static uint32_t px[ICON_PX * ICON_PX];
    for (int k = 0; k < PROMPT_COUNT; k++) {
        if (prompts_rasterize(k, px, ICON_PX) != 0)
            continue;
        /* Kenney's buttons sit inside a margin: draw just the button. */
        int x0 = ICON_PX, y0 = ICON_PX, x1 = -1, y1 = -1;
        for (int y = 0; y < ICON_PX; y++)
            for (int x = 0; x < ICON_PX; x++)
                if ((px[y * ICON_PX + x] >> 24) > 24) {
                    x0 = x < x0 ? x : x0;
                    x1 = x > x1 ? x : x1;
                    y0 = y < y0 ? y : y0;
                    y1 = y > y1 ? y : y1;
                }
        drawn[k] = x1 < 0 ? (SDL_Rect){ 0, 0, ICON_PX, ICON_PX }
                          : (SDL_Rect){ x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
        icons[k] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                     ICON_PX, ICON_PX);
        if (!icons[k])
            continue;
        SDL_UpdateTexture(icons[k], NULL, px, ICON_PX * 4);
        SDL_SetTextureBlendMode(icons[k], SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(icons[k], SDL_ScaleModeLinear);
    }
}

void prompts_cleanup(void)
{
    for (int k = 0; k < PROMPT_COUNT; k++)
        if (icons[k]) {
            SDL_DestroyTexture(icons[k]);
            icons[k] = NULL;
        }
    icons_for = NULL;
}

/* An icon is 14 px per font scale step high (the text is 8), as wide as its shape,
 * with 3 of space after it. */
#define ICON_H 14
static int icon_w(int kind, int scale)
{
    const SDL_Rect *d = &drawn[kind];
    return d->h > 0 ? (ICON_H * scale * d->w + d->h / 2) / d->h : ICON_H * scale;
}
static int gap(int scale)    { return 3 * scale; }

static int keyboard;                    /* tokens as keycaps */

void prompts_set_keyboard(int on) { keyboard = on; }
int  prompts_keyboard(void)       { return keyboard; }

/* A keycap: the key's name in a box as high as an icon. */
static int cap_w(int kind, int scale)
{
    return font_width(prompts_key_label(kind), scale) + 6 * scale;
}

static void draw_cap(SDL_Renderer *ren, int kind, int x, int y, int scale, SDL_Color c)
{
    int w = cap_w(kind, scale), h = ICON_H * scale, t = scale > 1 ? scale / 2 + 1 : 1;
    SDL_BlendMode bm;
    SDL_GetRenderDrawBlendMode(ren, &bm);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 40);
    SDL_Rect fill = { x, y, w, h };
    SDL_RenderFillRect(ren, &fill);
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 200);
    SDL_Rect edges[4] = { { x, y, w, t }, { x, y + h - t, w, t }, { x, y, t, h }, { x + w - t, y, t, h } };
    SDL_RenderFillRects(ren, edges, 4);
    SDL_SetRenderDrawBlendMode(ren, bm);
    font_draw(ren, prompts_key_label(kind), x + 3 * scale, y + 3 * scale, scale, c);
}

static int token_w(int kind, int scale)
{
    return keyboard ? cap_w(kind, scale) : icon_w(kind, scale);
}

int prompts_width(SDL_Renderer *ren, const char *text, int scale)
{
    load_icons(ren);                        /* (sizes come from the icons) */
    int w = 0, kind;
    char run[256];
    for (size_t n; (n = prompts_next(text, &kind)) > 0; text += n) {
        if (kind != PROMPT_TEXT) {
            w += token_w(kind, scale) + gap(scale);
        } else {
            snprintf(run, sizeof(run), "%.*s", (int)n, text);
            w += font_width(run, scale);
        }
    }
    return w;
}

int prompts_token_rect(SDL_Renderer *ren, const char *text, int x, int y, int scale, int want,
                       SDL_Rect *out)
{
    load_icons(ren);
    int kind;
    char run[256];
    for (size_t n; (n = prompts_next(text, &kind)) > 0; text += n) {
        if (kind != PROMPT_TEXT) {
            if (kind == want) {
                *out = (SDL_Rect){ x, y - 3 * scale, token_w(kind, scale), ICON_H * scale };
                return 0;
            }
            x += token_w(kind, scale) + gap(scale);
        } else {
            snprintf(run, sizeof(run), "%.*s", (int)n, text);
            x += font_width(run, scale);
        }
    }
    return -1;
}

int prompts_draw(SDL_Renderer *ren, const char *text, int x, int y, int scale, SDL_Color c)
{
    load_icons(ren);
    int x0 = x, kind;
    char run[256];
    for (size_t n; (n = prompts_next(text, &kind)) > 0; text += n) {
        if (kind != PROMPT_TEXT) {
            /* Centred on the 8-high text line: 3 above it, 3 below. */
            SDL_Rect r = { x, y - 3 * scale, token_w(kind, scale), ICON_H * scale };
            if (keyboard)
                draw_cap(ren, kind, r.x, r.y, scale, c);
            else if (icons[kind])
                SDL_RenderCopy(ren, icons[kind], &drawn[kind], &r);
            x += r.w + gap(scale);
        } else {
            snprintf(run, sizeof(run), "%.*s", (int)n, text);
            font_draw(ren, run, x, y, scale, c);
            x += font_width(run, scale);
        }
    }
    return x - x0;
}
