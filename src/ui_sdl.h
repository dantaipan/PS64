#ifndef UI_SDL_H
#define UI_SDL_H

#include "ui.h"

#include <SDL2/SDL.h>

/* Draw the current UI screen, if any, over the picture. */
void ui_draw(SDL_Renderer *ren, const ui_state *u, Uint32 now);

#endif
