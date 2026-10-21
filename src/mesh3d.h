/*
 * mesh3d - a few flat-shaded 3D objects for the home screen, drawn with
 * SDL_RenderGeometry (on the PS5 that's SDL's software renderer: keep meshes
 * small). Geometry and projection are pure C (tested); mesh3d_draw is in
 * mesh3d_sdl.c.
 *
 * Object space: x right, y up, z away from the viewer (the front of an object
 * faces -z). Triangles are stored wound so that, projected to the screen
 * (y down), a face turned towards the viewer has a positive cross product.
 */
#ifndef MESH3D_H
#define MESH3D_H

#include <stdint.h>

typedef struct { float x, y, z; } vec3;

typedef struct {
    vec3     p[3];
    float    u[3], v[3];    /* texture coordinates (0..1) when textured */
    uint32_t rgb;           /* 0xRRGGBB base colour */
    uint8_t  textured;      /* drawn with the object's texture (a screen, a label) */
    uint8_t  layer;         /* parts on top of others (knobs on a panel) draw after them */
} tri3;

typedef struct {
    tri3 *tris;
    int   count, cap;
    int   layer;            /* given to the triangles added from now on */
} mesh;

/* Where and how an object is drawn: its centre on screen, pixels per unit at
 * the object's centre, rotation (radians; yaw about y, then pitch about x) and
 * a brightness factor (1 = lit normally). */
typedef struct {
    float cx, cy, scale;
    float yaw, pitch;
    float bright;
} pose3;

#define MESH3D_CAMERA 3.0f  /* viewer distance from the object's centre, in units */

/* A triangle facing out along normal n (the winding is fixed up to match). */
void mesh_tri(mesh *m, vec3 a, vec3 b, vec3 c, vec3 n, uint32_t rgb);
/* A quad a-b-c-d (in order around its edge) facing out along n. */
void mesh_quad(mesh *m, vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, uint32_t rgb);
/* The same quad, textured (a top-left, b top-right, c bottom-right, d bottom-left of the
 * texture), split into nx x ny cells so affine texturing doesn't bend it visibly. */
void mesh_textured_quad(mesh *m, vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, int nx, int ny);

/* Faces of a box, for mesh_box's skip mask. */
enum { BOX_FRONT = 1, BOX_BACK = 2, BOX_LEFT = 4, BOX_RIGHT = 8, BOX_TOP = 16, BOX_BOTTOM = 32 };
/* An axis-aligned box from lo to hi, leaving out the faces in skip. */
void mesh_box(mesh *m, vec3 lo, vec3 hi, uint32_t rgb, unsigned skip);
/* A tapered box: a front rectangle at z0 and a back one at z1 (x0..x1, y0..y1 each),
 * joined by four sides; the front and back faces are left out when in skip. */
void mesh_frustum(mesh *m, float z0, const float front[4], float z1, const float back[4],
                  uint32_t rgb, unsigned skip);

void mesh_free(mesh *m);

/* A point through a pose: screen x, y and depth (larger is further away). */
void mesh_project(const pose3 *p, vec3 v, float *sx, float *sy, float *depth);

/* A projected, shaded triangle, ready to draw. */
typedef struct {
    float    x[3], y[3];
    float    u[3], v[3];
    float    depth;         /* sort key: distance, less a little per layer */
    uint32_t rgb;           /* shaded colour */
    uint8_t  textured;
} tri2;

/*
 * Project, cull the faces turned away, shade (flat, one light from the upper left
 * front) and sort far to near. Writes up to cap triangles to out, returns the count.
 */
int mesh_prepare(const mesh *m, const pose3 *p, tri2 *out, int cap);

#endif
