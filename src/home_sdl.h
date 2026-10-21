/*
 * home_sdl - draws the home screen (UI_HOME) on a 1920x1080 canvas: the
 * PS64 logo, power and settings icons, and the three 3D objects.
 */
#ifndef HOME_SDL_H
#define HOME_SDL_H

#include "ui.h"

#include <SDL2/SDL.h>

/* live: the C64 Ultimate's picture, shown on the 1084 while u->home_stream is live. */
void home_draw(SDL_Renderer *ren, const ui_state *u, SDL_Texture *live, Uint32 now);

/* Free the home screen's textures (call before destroying the renderer). */
void home_cleanup(void);

#endif
