/*
 * font_sdl - 8x8 text and the overlay canvas shared by the on-screen
 * keyboard and the menus.
 */
#ifndef FONT_SDL_H
#define FONT_SDL_H

#include <SDL2/SDL.h>

/* Overlays are laid out on a 768x544 canvas (2x a PAL frame) and scaled
 * with the picture. */
#define CANVAS_W 768
#define CANVAS_H 544

/* Text at scale x 8 pixels per character; bytes outside the font are skipped. */
void font_draw(SDL_Renderer *ren, const char *text, int x, int y, int scale, SDL_Color c);
int  font_width(const char *text, int scale);

/* Switch the renderer to the overlay canvas with blending on; canvas_end restores it. */
void canvas_begin(SDL_Renderer *ren, int *saved_w, int *saved_h);
void canvas_end(SDL_Renderer *ren, int saved_w, int saved_h);

void fill_rect(SDL_Renderer *ren, int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a);

/* Free the cached font texture (call before destroying the renderer). */
void font_cleanup(void);

#endif
