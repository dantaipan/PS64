#ifndef OSK_SDL_H
#define OSK_SDL_H

#include "osk.h"

#include <SDL2/SDL.h>

/* Draw the on-screen keyboard over the bottom of the picture. hints: its own button
 * hints (typing onto the C64); a PS64 screen typing with it shows its own instead. */
void osk_draw(SDL_Renderer *ren, const osk_state *s, int hints);

/* Draw a one-line message at the top of the picture (e.g. "Joystick port 1"). */
void osk_draw_banner(SDL_Renderer *ren, const char *text);

#endif
