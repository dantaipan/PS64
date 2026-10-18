#include "font_sdl.h"

#include "font8x8.h"

#include <string.h>

#define GLYPHS (FONT_LAST - FONT_FIRST + 1)

static SDL_Texture *font_tex;
static SDL_Renderer *font_owner;

static SDL_Texture *font_texture(SDL_Renderer *ren)
{
    if (font_tex && font_owner == ren)
        return font_tex;
    if (font_tex)
        SDL_DestroyTexture(font_tex);
    font_owner = ren;
    static uint32_t pixels[GLYPHS * 8 * 8];
    for (int g = 0; g < GLYPHS; g++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                pixels[y * GLYPHS * 8 + g * 8 + x] =
                    (font8x8[g][y] >> x) & 1 ? 0xFFFFFFFF : 0x00FFFFFF;
    font_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                 GLYPHS * 8, 8);
    if (font_tex) {
        SDL_UpdateTexture(font_tex, NULL, pixels, GLYPHS * 8 * 4);
        SDL_SetTextureBlendMode(font_tex, SDL_BLENDMODE_BLEND);
    }
    return font_tex;
}

void font_cleanup(void)
{
    if (font_tex)
        SDL_DestroyTexture(font_tex);
    font_tex = NULL;
    font_owner = NULL;
}

int font_width(const char *text, int scale)
{
    return (int)strlen(text) * 8 * scale;
}

void font_draw(SDL_Renderer *ren, const char *text, int x, int y, int scale, SDL_Color c)
{
    SDL_Texture *tex = font_texture(ren);
    if (!tex)
        return;
    SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(tex, c.a);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++, x += 8 * scale) {
        if (*p < FONT_FIRST || *p > FONT_LAST)
            continue;
        SDL_Rect src = { (*p - FONT_FIRST) * 8, 0, 8, 8 };
        SDL_Rect dst = { x, y, 8 * scale, 8 * scale };
        SDL_RenderCopy(ren, tex, &src, &dst);
    }
}

void canvas_begin(SDL_Renderer *ren, int *saved_w, int *saved_h)
{
    SDL_RenderGetLogicalSize(ren, saved_w, saved_h);
    SDL_RenderSetLogicalSize(ren, CANVAS_W, CANVAS_H);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
}

void canvas_end(SDL_Renderer *ren, int saved_w, int saved_h)
{
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_RenderSetLogicalSize(ren, saved_w, saved_h);
}

void fill_rect(SDL_Renderer *ren, int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_Rect rect = { x, y, w, h };
    SDL_SetRenderDrawColor(ren, r, g, b, a);
    SDL_RenderFillRect(ren, &rect);
}
