/*
 * prompts_sdl - draws hint lines with controller button icons (see prompts.h):
 * text in the 8x8 font, each "{token}" as its button, 1.5x the font's height.
 */
#ifndef PROMPTS_SDL_H
#define PROMPTS_SDL_H

#include <SDL2/SDL.h>

/* Draw text with its tokens as icons at x, y (the text's top); returns the width. */
int  prompts_draw(SDL_Renderer *ren, const char *text, int x, int y, int scale, SDL_Color c);
int  prompts_width(SDL_Renderer *ren, const char *text, int scale);

/* Where prompts_draw (same text, x, y, scale) puts the first {token} of kind: its rect,
 * 0; -1 if the text has none. */
int  prompts_token_rect(SDL_Renderer *ren, const char *text, int x, int y, int scale, int kind,
                        SDL_Rect *out);
/* Whether tokens are drawn as keycaps right now. */
int  prompts_keyboard(void);

/* Draw the tokens as Mac keyboard keycaps instead (no controller connected). */
void prompts_set_keyboard(int on);

/* Free the icon textures (call before destroying the renderer). */
void prompts_cleanup(void);

#endif
