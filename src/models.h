/*
 * models - the home screen's objects, built from boxes: a Commodore
 * 1702-style monitor, a 1084-style monitor and a SID chip (28-pin DIP). About
 * one unit wide, centred on the origin, front facing -z (see mesh3d.h). The
 * textured faces are the monitors' screens and the chip's printed top.
 */
#ifndef MODELS_H
#define MODELS_H

#include "mesh3d.h"

/*
 * The monitors' printed labels: rectangles (x, y, w, h in pixels) of their decal textures
 * (DECAL_W x DECAL_H; home_1702_decals / home_1084_decals draw them).
 */
#define DECAL_W 256
#define DECAL_H 128
typedef struct { int x, y, w, h; } decal_rect;
static const decal_rect DECAL_1702_BADGE = { 0, 0, 160, 24 };    /* C= commodore, rainbow, POWER window */
static const decal_rect DECAL_1702_MODEL = { 0, 32, 112, 24 };   /* VIDEO MONITOR / MODEL 1702 */
static const decal_rect DECAL_1084_LABEL = { 0, 0, 72, 20 };     /* 1084 in its box */
static const decal_rect DECAL_1084_LOGO  = { 0, 24, 96, 16 };    /* C= Commodore */
static const decal_rect DECAL_1084_POWER = { 0, 44, 40, 20 };    /* POWER and the power sign */

void model_1702(mesh *m);
void model_1084(mesh *m);
/* An old portable TV set (the VIC-20's): cream case, screen on the left, controls on the
 * right, an antenna. */
void model_tv(mesh *m);
/* The chip's body and its two rows of 14 pins (the row on the -z side, then +z): drawn
 * as the farther row, the body, then the nearer row, so they always overlap right. */
void model_sid(mesh *body, mesh *row_near, mesh *row_far);

#endif
