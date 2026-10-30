#include "models.h"

#include <math.h>

static const vec3 FRONT = { 0, 0, -1 };

/* Move every vertex (to centre a model on the origin). */
static void translate(mesh *m, float dx, float dy, float dz)
{
    for (int i = 0; i < m->count; i++)
        for (int k = 0; k < 3; k++) {
            m->tris[i].p[k].x += dx;
            m->tris[i].p[k].y += dy;
            m->tris[i].p[k].z += dz;
        }
}

/* A flat front-facing frame at depth z: the area between outer o and inner i
 * (x0, x1, y0, y1 each). */
static void frame(mesh *m, const float o[4], const float i[4], float z, uint32_t rgb)
{
    mesh_quad(m, (vec3){o[0], o[3], z}, (vec3){o[1], o[3], z}, (vec3){o[1], i[3], z},
              (vec3){o[0], i[3], z}, FRONT, rgb);                                  /* top */
    mesh_quad(m, (vec3){o[0], i[2], z}, (vec3){o[1], i[2], z}, (vec3){o[1], o[2], z},
              (vec3){o[0], o[2], z}, FRONT, rgb);                                  /* bottom */
    mesh_quad(m, (vec3){o[0], i[3], z}, (vec3){i[0], i[3], z}, (vec3){i[0], i[2], z},
              (vec3){o[0], i[2], z}, FRONT, rgb);                                  /* left */
    mesh_quad(m, (vec3){i[1], i[3], z}, (vec3){o[1], i[3], z}, (vec3){o[1], i[2], z},
              (vec3){i[1], i[2], z}, FRONT, rgb);                                  /* right */
}

/* The walls of a screen's recess, from the opening o at z0 back to the screen s at z1,
 * facing into the opening. */
static void recess(mesh *m, const float o[4], float z0, const float s[4], float z1, uint32_t rgb)
{
    vec3 a = { o[0], o[3], z0 }, b = { o[1], o[3], z0 }, c = { o[1], o[2], z0 }, d = { o[0], o[2], z0 };
    vec3 A = { s[0], s[3], z1 }, B = { s[1], s[3], z1 }, C = { s[1], s[2], z1 }, D = { s[0], s[2], z1 };
    mesh_quad(m, a, b, B, A, (vec3){0, -1, 0}, rgb);       /* top wall faces down */
    mesh_quad(m, d, c, C, D, (vec3){0, 1, 0}, rgb);        /* bottom wall faces up */
    mesh_quad(m, a, d, D, A, (vec3){1, 0, 0}, rgb);        /* left wall faces right */
    mesh_quad(m, b, c, C, B, (vec3){-1, 0, 0}, rgb);       /* right wall faces left */
}

/* A small part standing out of a front face at depth z by depth d. */
static void stud(mesh *m, float x0, float x1, float y0, float y1, float z, float d, uint32_t rgb)
{
    mesh_box(m, (vec3){x0, y0, z - d}, (vec3){x1, y1, z}, rgb, BOX_BACK);
}

void model_1702(mesh *m)
{
    const uint32_t CASE = 0xB4A68C, PANEL = 0xA89A7F, BEZEL = 0x2A2A2C, WALL = 0x1E1E21;
    /* Case without its front; the front is a frame round the bezel. */
    mesh_box(m, (vec3){-0.45f, -0.43f, 0}, (vec3){0.45f, 0.43f, 0.9f}, CASE, BOX_FRONT);
    /* Nearly square, with a 4:3 screen (0.667 x 0.5), like the real one. */
    const float body[4] = { -0.45f, 0.45f, -0.43f, 0.43f };
    const float bezel[4] = { -0.40f, 0.40f, -0.22f, 0.40f };
    const float open[4] = { -0.3333f, 0.3333f, -0.16f, 0.34f };
    frame(m, body, bezel, 0, CASE);
    recess(m, open, -0.03f, open, 0.05f, WALL);
    /* Black bezel standing out of the case, with the screen set back inside it. */
    m->layer = 1;
    frame(m, bezel, open, -0.03f, BEZEL);
    mesh_frustum(m, -0.03f, bezel, 0, bezel, BEZEL, BOX_FRONT | BOX_BACK);
    m->layer = 0;
    mesh_textured_quad(m, (vec3){open[0], open[3], 0.05f}, (vec3){open[1], open[3], 0.05f},
                       (vec3){open[1], open[2], 0.05f}, (vec3){open[0], open[2], 0.05f}, FRONT, 8, 6);
    /* Control strip below the screen: the commodore badge in the middle with its rainbow
     * and power light, the brown power button on the right. */
    static const uint32_t rainbow[5] = { 0xE03A2F, 0xF08A24, 0xF5D02E, 0x3FA34D, 0x3B7DD8 };
    m->layer = 1;
    stud(m, -0.41f, 0.41f, -0.40f, -0.27f, 0, 0.02f, PANEL);
    m->layer = 2;
    stud(m, -0.13f, 0.13f, -0.357f, -0.313f, -0.02f, 0.005f, 0x55585E);
    stud(m, 0.30f, 0.35f, -0.375f, -0.325f, -0.02f, 0.02f, 0x8A6A48);
    m->layer = 3;
    for (int i = 0; i < 5; i++)
        stud(m, 0.01f, 0.07f, -0.320f - i * 0.0076f, -0.3145f - i * 0.0076f, -0.025f, 0.002f, rainbow[i]);
    stud(m, 0.09f, 0.11f, -0.340f, -0.330f, -0.025f, 0.004f, 0xD03A2E);
    m->layer = 0;
    translate(m, 0, 0, -0.43f);
}

void model_1084(mesh *m)
{
    const uint32_t CASE = 0xE6E2D7, BACK = 0xD3CEC0, WALL = 0xB9B4A8;
    /* Front section, its front edges chamfered so it looks rounded. */
    const float outer[4] = { -0.46f, 0.46f, -0.44f, 0.42f };
    const float face[4] = { -0.43f, 0.43f, -0.41f, 0.39f };
    const float open[4] = { -0.36f, 0.36f, -0.22f, 0.33f };
    const float screen[4] = { -0.327f, 0.327f, -0.19f, 0.30f };     /* 4:3 */
    mesh_box(m, (vec3){outer[0], outer[2], 0.02f}, (vec3){outer[1], outer[3], 0.32f}, CASE,
             BOX_FRONT | BOX_BACK);
    mesh_frustum(m, -0.02f, face, 0.02f, outer, CASE, BOX_FRONT | BOX_BACK);
    frame(m, face, open, -0.02f, CASE);
    /* The picture tube's surround slopes in to the screen. */
    recess(m, open, -0.02f, screen, 0.06f, WALL);
    mesh_textured_quad(m, (vec3){screen[0], screen[3], 0.06f}, (vec3){screen[1], screen[3], 0.06f},
                       (vec3){screen[1], screen[2], 0.06f}, (vec3){screen[0], screen[2], 0.06f},
                       FRONT, 8, 6);
    /* Deep tapered back. */
    const float back[4] = { -0.28f, 0.28f, -0.36f, 0.18f };
    mesh_frustum(m, 0.32f, outer, 0.85f, back, BACK, BOX_FRONT);
    /* Badge, the seam of the control door, power light. */
    m->layer = 1;
    stud(m, -0.36f, -0.12f, -0.335f, -0.300f, -0.02f, 0.005f, 0x55585E);
    stud(m, -0.40f, 0.40f, -0.272f, -0.264f, -0.02f, 0.003f, 0xC4BFB2);
    stud(m, 0.28f, 0.32f, -0.345f, -0.325f, -0.02f, 0.006f, 0xD03A2E);
    m->layer = 0;
    translate(m, 0, 0, -0.40f);
}

void model_sid(mesh *m, mesh *row_near, mesh *row_far)
{
    const uint32_t BODY = 0x26262A, PIN = 0xB8BCC4;
    /* Body lying flat (length along x, width along z); its top is the printed label. */
    mesh_box(m, (vec3){-0.6f, -0.06f, -0.2f}, (vec3){0.6f, 0.06f, 0.2f}, BODY, BOX_TOP);
    mesh_textured_quad(m, (vec3){-0.6f, 0.06f, 0.2f}, (vec3){0.6f, 0.06f, 0.2f},
                       (vec3){0.6f, 0.06f, -0.2f}, (vec3){-0.6f, 0.06f, -0.2f},
                       (vec3){0, 1, 0}, 4, 2);
    /* 14 pins down each long side: out from the body, then bent down. */
    for (int i = 0; i < 14; i++) {
        float x = -0.52f + i * 0.08f;
        for (int side = -1; side <= 1; side += 2) {
            mesh *row = side < 0 ? row_near : row_far;
            float z0 = side * 0.2f, z1 = side * 0.27f, zl = side * 0.25f;
            mesh_box(row, (vec3){x - 0.022f, -0.012f, side < 0 ? z1 : z0},
                     (vec3){x + 0.022f, 0.012f, side < 0 ? z0 : z1}, PIN,
                     side < 0 ? BOX_BACK : BOX_FRONT);
            mesh_box(row, (vec3){x - 0.012f, -0.22f, side < 0 ? z1 : zl},
                     (vec3){x + 0.012f, -0.012f, side < 0 ? zl : z1}, PIN, BOX_TOP);
        }
    }
}

/* A short n-sided prism standing out of a front face at depth z by depth d (a dial or
 * knob): its front face and its sides. */
static void dial(mesh *m, float cx, float cy, float r, float z, float d, int sides,
                 uint32_t face, uint32_t side)
{
    for (int i = 0; i < sides; i++) {
        float a0 = 6.2831853f * i / sides, a1 = 6.2831853f * (i + 1) / sides;
        vec3 p0 = { cx + r * cosf(a0), cy + r * sinf(a0), z - d };
        vec3 p1 = { cx + r * cosf(a1), cy + r * sinf(a1), z - d };
        mesh_tri(m, (vec3){cx, cy, z - d}, p1, p0, FRONT, face);
        vec3 q0 = { p0.x, p0.y, z }, q1 = { p1.x, p1.y, z };
        float am = (a0 + a1) / 2;
        mesh_quad(m, p0, p1, q1, q0, (vec3){cosf(am), sinf(am), 0}, side);
    }
}

void model_tv(mesh *m)
{
    const uint32_t CASE = 0xE9DFB2, CASE_SIDE = 0xD8CC9C, BEZEL = 0x1C1C1E, TRIM = 0xB4B6B8,
                   WALL = 0x2A2A2C, PANEL = 0x1E2A30, GRILLE = 0x18181A, RIDGE = 0x2C2C30,
                   PLATE = 0xD4D7DB, RING = 0x34363A, DIAL = 0xA8ADB4, KNOB = 0x5A5E64,
                   CHROME = 0xC8CCD0, PLINTH = 0x151517;
    /* Cream case, its front edges chamfered; the plinth under it. */
    const float outer[4] = { -0.50f, 0.50f, -0.30f, 0.30f };
    const float face[4] = { -0.48f, 0.48f, -0.28f, 0.28f };
    mesh_box(m, (vec3){outer[0], outer[2], 0.02f}, (vec3){outer[1], outer[3], 0.62f}, CASE_SIDE,
             BOX_FRONT);
    mesh_frustum(m, -0.0f, face, 0.02f, outer, CASE, BOX_FRONT | BOX_BACK);
    mesh_box(m, (vec3){-0.42f, -0.35f, 0.08f}, (vec3){0.42f, -0.30f, 0.56f}, PLINTH, BOX_TOP);
    /* The front: a black bezel round the picture (left), the control column (right). */
    const float bezel[4] = { -0.46f, 0.16f, -0.26f, 0.26f };
    const float trim[4] = { -0.435f, 0.135f, -0.235f, 0.235f };
    const float open[4] = { -0.40f, 0.10f, -0.20f, 0.20f };
    const float screen[4] = { -0.38f, 0.08f, -0.18f, 0.18f };      /* 4:3 */
    const float column[4] = { 0.19f, 0.46f, -0.26f, 0.26f };
    /* The cream face between and around them. */
    mesh_quad(m, (vec3){face[0], face[3], 0}, (vec3){face[1], face[3], 0},
              (vec3){face[1], bezel[3], 0}, (vec3){face[0], bezel[3], 0}, FRONT, CASE);
    mesh_quad(m, (vec3){face[0], bezel[2], 0}, (vec3){face[1], bezel[2], 0},
              (vec3){face[1], face[2], 0}, (vec3){face[0], face[2], 0}, FRONT, CASE);
    mesh_quad(m, (vec3){face[0], bezel[3], 0}, (vec3){bezel[0], bezel[3], 0},
              (vec3){bezel[0], bezel[2], 0}, (vec3){face[0], bezel[2], 0}, FRONT, CASE);
    mesh_quad(m, (vec3){bezel[1], bezel[3], 0}, (vec3){column[0], bezel[3], 0},
              (vec3){column[0], bezel[2], 0}, (vec3){bezel[1], bezel[2], 0}, FRONT, CASE);
    mesh_quad(m, (vec3){column[1], bezel[3], 0}, (vec3){face[1], bezel[3], 0},
              (vec3){face[1], bezel[2], 0}, (vec3){column[1], bezel[2], 0}, FRONT, CASE);
    /* Bezel, grey trim, and the screen set back in its recess. */
    m->layer = 1;
    frame(m, bezel, trim, -0.01f, BEZEL);
    frame(m, trim, open, -0.012f, TRIM);
    recess(m, open, -0.012f, screen, 0.05f, WALL);
    mesh_textured_quad(m, (vec3){screen[0], screen[3], 0.05f}, (vec3){screen[1], screen[3], 0.05f},
                       (vec3){screen[1], screen[2], 0.05f}, (vec3){screen[0], screen[2], 0.05f},
                       FRONT, 8, 6);
    /* Control column: grilles at the top and bottom, the dark panel between with the
     * silver plate (two dials) and three small knobs. */
    stud(m, column[0], column[1], 0.17f, 0.26f, 0, 0.005f, GRILLE);
    stud(m, column[0], column[1], -0.26f, -0.06f, 0, 0.005f, GRILLE);
    stud(m, column[0], column[1], -0.06f, 0.17f, 0, 0.003f, PANEL);
    m->layer = 2;
    for (int i = 0; i < 3; i++)
        stud(m, column[0] + 0.02f, column[1] - 0.02f, 0.19f + i * 0.025f, 0.2f + i * 0.025f,
             -0.005f, 0.004f, RIDGE);
    for (int i = 0; i < 6; i++)
        stud(m, column[0] + 0.02f, column[1] - 0.02f, -0.24f + i * 0.03f, -0.23f + i * 0.03f,
             -0.005f, 0.004f, RIDGE);
    stud(m, 0.215f, 0.365f, -0.045f, 0.155f, -0.003f, 0.006f, PLATE);
    m->layer = 3;
    /* Each dial: a dark numbered ring with the silver knob standing out of it. */
    for (int i = 0; i < 2; i++)
        dial(m, 0.29f, 0.10f - i * 0.10f, 0.045f, -0.009f, 0.012f, 12, RING, RING);
    for (int i = 0; i < 3; i++)
        dial(m, 0.415f, 0.12f - i * 0.065f, 0.015f, -0.003f, 0.015f, 6, KNOB, 0x3A3D42);
    m->layer = 4;
    for (int i = 0; i < 2; i++)
        dial(m, 0.29f, 0.10f - i * 0.10f, 0.030f, -0.021f, 0.02f, 10, DIAL, 0x7E838A);
    m->layer = 5;
    stud(m, 0.262f, 0.318f, 0.095f, 0.105f, -0.041f, 0.004f, RING);      /* the dials' pointers */
    stud(m, 0.262f, 0.318f, -0.005f, 0.005f, -0.041f, 0.004f, RING);
    m->layer = 0;
    /* The antenna: a thin chrome rod from the top right, leaning back to the left. */
    const vec3 base = { 0.24f, 0.30f, 0.50f }, tip = { 0.06f, 0.54f, 0.50f };
    const float t = 0.007f;
    vec3 a = { base.x - t, base.y, base.z - t }, b = { base.x + t, base.y, base.z - t },
         c = { tip.x + t, tip.y, tip.z - t }, d = { tip.x - t, tip.y, tip.z - t };
    mesh_quad(m, d, c, b, a, FRONT, CHROME);
    vec3 a2 = { base.x - t, base.y, base.z + t }, b2 = { base.x + t, base.y, base.z + t },
         c2 = { tip.x + t, tip.y, tip.z + t }, d2 = { tip.x - t, tip.y, tip.z + t };
    mesh_quad(m, c, c2, b2, b, (vec3){1, 0.6f, 0}, CHROME);
    mesh_quad(m, d2, d, a, a2, (vec3){-1, -0.6f, 0}, CHROME);
    mesh_quad(m, c2, d2, a2, b2, (vec3){0, 0, 1}, CHROME);
    mesh_box(m, (vec3){0.21f, 0.30f, 0.46f}, (vec3){0.27f, 0.32f, 0.54f}, CHROME, BOX_BOTTOM);
    translate(m, 0, 0.0f, -0.30f);
}
