#ifndef OSK_SDL_H
#define OSK_SDL_H

#include "osk.h"

#include <SDL2/SDL.h>

/* Draw the on-screen keyboard over the bottom of the picture. */
void osk_draw(SDL_Renderer *ren, const osk_state *s);

/* Draw a one-line message at the top of the picture (e.g. "Joystick port 1"). */
void osk_draw_banner(SDL_Renderer *ren, const char *text);

#endif
