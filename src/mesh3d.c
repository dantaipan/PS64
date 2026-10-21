#include "mesh3d.h"

#include <math.h>
#include <stdlib.h>

static vec3 sub(vec3 a, vec3 b) { return (vec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static vec3 cross(vec3 a, vec3 b)
{
    return (vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

static tri3 *add(mesh *m)
{
    if (m->count == m->cap) {
        int cap = m->cap ? m->cap * 2 : 64;
        tri3 *t = realloc(m->tris, (size_t)cap * sizeof(*t));
        if (!t)
            return NULL;
        m->tris = t;
        m->cap = cap;
    }
    tri3 *t = &m->tris[m->count++];
    *t = (tri3){0};
    return t;
}

/* Store a triangle wound so its geometric normal, cross(b - a, c - a), points along n:
 * facing the viewer, that projects (y down) to a positive cross product. */
static void put(mesh *m, vec3 a, vec3 b, vec3 c, vec3 n, uint32_t rgb, int textured,
                const float *u, const float *v)
{
    tri3 *t = add(m);
    if (!t)
        return;
    int flip = dot(cross(sub(b, a), sub(c, a)), n) < 0;
    t->p[0] = a;
    t->p[1] = flip ? c : b;
    t->p[2] = flip ? b : c;
    t->rgb = rgb;
    t->textured = (uint8_t)textured;
    t->layer = (uint8_t)m->layer;
    if (textured) {
        t->u[0] = u[0]; t->u[1] = u[flip ? 2 : 1]; t->u[2] = u[flip ? 1 : 2];
        t->v[0] = v[0]; t->v[1] = v[flip ? 2 : 1]; t->v[2] = v[flip ? 1 : 2];
    }
}

void mesh_tri(mesh *m, vec3 a, vec3 b, vec3 c, vec3 n, uint32_t rgb)
{
    put(m, a, b, c, n, rgb, 0, NULL, NULL);
}

void mesh_quad(mesh *m, vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, uint32_t rgb)
{
    put(m, a, b, c, n, rgb, 0, NULL, NULL);
    put(m, a, c, d, n, rgb, 0, NULL, NULL);
}

static vec3 lerp2(vec3 a, vec3 b, vec3 c, vec3 d, float s, float t)
{
    /* Bilinear over a (0,0), b (1,0), c (1,1), d (0,1). */
    vec3 top = { a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s, a.z + (b.z - a.z) * s };
    vec3 bot = { d.x + (c.x - d.x) * s, d.y + (c.y - d.y) * s, d.z + (c.z - d.z) * s };
    return (vec3){ top.x + (bot.x - top.x) * t, top.y + (bot.y - top.y) * t,
                   top.z + (bot.z - top.z) * t };
}

void mesh_textured_quad(mesh *m, vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, int nx, int ny)
{
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            float s0 = (float)i / nx, s1 = (float)(i + 1) / nx;
            float t0 = (float)j / ny, t1 = (float)(j + 1) / ny;
            vec3 p00 = lerp2(a, b, c, d, s0, t0), p10 = lerp2(a, b, c, d, s1, t0);
            vec3 p11 = lerp2(a, b, c, d, s1, t1), p01 = lerp2(a, b, c, d, s0, t1);
            float u1[3] = { s0, s1, s1 }, v1[3] = { t0, t0, t1 };
            float u2[3] = { s0, s1, s0 }, v2[3] = { t0, t1, t1 };
            put(m, p00, p10, p11, n, 0xFFFFFF, 1, u1, v1);
            put(m, p00, p11, p01, n, 0xFFFFFF, 1, u2, v2);
        }
}

void mesh_box(mesh *m, vec3 lo, vec3 hi, uint32_t rgb, unsigned skip)
{
    float x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hi.x, y1 = hi.y, z1 = hi.z;
    if (!(skip & BOX_FRONT))
        mesh_quad(m, (vec3){x0, y1, z0}, (vec3){x1, y1, z0}, (vec3){x1, y0, z0},
                  (vec3){x0, y0, z0}, (vec3){0, 0, -1}, rgb);
    if (!(skip & BOX_BACK))
        mesh_quad(m, (vec3){x0, y1, z1}, (vec3){x1, y1, z1}, (vec3){x1, y0, z1},
                  (vec3){x0, y0, z1}, (vec3){0, 0, 1}, rgb);
    if (!(skip & BOX_LEFT))
        mesh_quad(m, (vec3){x0, y1, z0}, (vec3){x0, y1, z1}, (vec3){x0, y0, z1},
                  (vec3){x0, y0, z0}, (vec3){-1, 0, 0}, rgb);
    if (!(skip & BOX_RIGHT))
        mesh_quad(m, (vec3){x1, y1, z0}, (vec3){x1, y1, z1}, (vec3){x1, y0, z1},
                  (vec3){x1, y0, z0}, (vec3){1, 0, 0}, rgb);
    if (!(skip & BOX_TOP))
        mesh_quad(m, (vec3){x0, y1, z0}, (vec3){x1, y1, z0}, (vec3){x1, y1, z1},
                  (vec3){x0, y1, z1}, (vec3){0, 1, 0}, rgb);
    if (!(skip & BOX_BOTTOM))
        mesh_quad(m, (vec3){x0, y0, z0}, (vec3){x1, y0, z0}, (vec3){x1, y0, z1},
                  (vec3){x0, y0, z1}, (vec3){0, -1, 0}, rgb);
}

void mesh_frustum(mesh *m, float z0, const float f[4], float z1, const float k[4],
                  uint32_t rgb, unsigned skip)
{
    /* f, k: x0, x1, y0, y1 of the front and back rectangles. */
    vec3 a = { f[0], f[3], z0 }, b = { f[1], f[3], z0 }, c = { f[1], f[2], z0 }, d = { f[0], f[2], z0 };
    vec3 A = { k[0], k[3], z1 }, B = { k[1], k[3], z1 }, C = { k[1], k[2], z1 }, D = { k[0], k[2], z1 };
    if (!(skip & BOX_FRONT))
        mesh_quad(m, a, b, c, d, (vec3){0, 0, -1}, rgb);
    if (!(skip & BOX_BACK))
        mesh_quad(m, A, B, C, D, (vec3){0, 0, 1}, rgb);
    /* Side normals: roughly outwards (the winding only needs the sign right). */
    mesh_quad(m, a, b, B, A, (vec3){0, 1, 0}, rgb);       /* top */
    mesh_quad(m, d, c, C, D, (vec3){0, -1, 0}, rgb);      /* bottom */
    mesh_quad(m, a, d, D, A, (vec3){-1, 0, 0}, rgb);      /* left */
    mesh_quad(m, b, c, C, B, (vec3){1, 0, 0}, rgb);       /* right */
}

void mesh_free(mesh *m)
{
    free(m->tris);
    *m = (mesh){0};
}

static vec3 rotate(const pose3 *p, vec3 v)
{
    float cy = cosf(p->yaw), sy = sinf(p->yaw), cp = cosf(p->pitch), sp = sinf(p->pitch);
    vec3 r = { v.x * cy + v.z * sy, v.y, -v.x * sy + v.z * cy };          /* yaw about y */
    return (vec3){ r.x, r.y * cp - r.z * sp, r.y * sp + r.z * cp };         /* pitch about x */
}

static void project_rotated(const pose3 *p, vec3 r, float *sx, float *sy, float *depth)
{
    float z = r.z + MESH3D_CAMERA;
    if (z < 0.1f)
        z = 0.1f;
    float k = p->scale * MESH3D_CAMERA / z;
    *sx = p->cx + r.x * k;
    *sy = p->cy - r.y * k;
    *depth = z;
}

void mesh_project(const pose3 *p, vec3 v, float *sx, float *sy, float *depth)
{
    project_rotated(p, rotate(p, v), sx, sy, depth);
}

static int by_depth(const void *a, const void *b)
{
    float da = ((const tri2 *)a)->depth, db = ((const tri2 *)b)->depth;
    return da < db ? 1 : da > db ? -1 : 0;                                  /* far first */
}

static uint32_t shade(uint32_t rgb, float k)
{
    int r = (int)(((rgb >> 16) & 255) * k), g = (int)(((rgb >> 8) & 255) * k), b = (int)((rgb & 255) * k);
    r = r > 255 ? 255 : r;
    g = g > 255 ? 255 : g;
    b = b > 255 ? 255 : b;
    return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

int mesh_prepare(const mesh *m, const pose3 *p, tri2 *out, int cap)
{
    /* Light from the upper left, in front of the objects. */
    const vec3 light = { -0.40f, 0.55f, -0.73f };
    int n = 0;
    for (int i = 0; i < m->count && n < cap; i++) {
        const tri3 *t = &m->tris[i];
        vec3 r[3];
        tri2 *o = &out[n];
        float depth = 0;
        for (int k = 0; k < 3; k++) {
            r[k] = rotate(p, t->p[k]);
            float d;
            project_rotated(p, r[k], &o->x[k], &o->y[k], &d);
            depth += d;
        }
        float area = (o->x[1] - o->x[0]) * (o->y[2] - o->y[0]) - (o->y[1] - o->y[0]) * (o->x[2] - o->x[0]);
        if (area <= 0)
            continue;                                   /* turned away from the viewer */
        vec3 nrm = cross(sub(r[1], r[0]), sub(r[2], r[0]));
        float len = sqrtf(dot(nrm, nrm));
        /* The stored winding gives outward normals (see put). */
        float lit = len > 0 ? dot(nrm, light) / len : 0;
        float k = 0.55f + 0.5f * (lit > 0 ? lit : 0);
        /* A part's whole surface can sort behind the panel it sits on when turned: layers
         * keep details in front of what they're mounted on. */
        o->depth = depth / 3 - 0.12f * t->layer;
        o->textured = t->textured;
        o->rgb = t->textured ? 0xFFFFFF : shade(t->rgb, k * p->bright);
        for (int j = 0; j < 3; j++) {
            o->u[j] = t->u[j];
            o->v[j] = t->v[j];
        }
        n++;
    }
    qsort(out, (size_t)n, sizeof(*out), by_depth);
    return n;
}
