/*
 * models - the home screen's three objects, built from boxes: a Commodore
 * 1702-style monitor, a 1084-style monitor and a SID chip (28-pin DIP). About
 * one unit wide, centred on the origin, front facing -z (see mesh3d.h). The
 * textured faces are the monitors' screens and the chip's printed top.
 */
#ifndef MODELS_H
#define MODELS_H

#include "mesh3d.h"

void model_1702(mesh *m);
void model_1084(mesh *m);
void model_sid(mesh *m);

#endif
