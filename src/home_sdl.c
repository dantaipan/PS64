#include "home_sdl.h"

#include "font_sdl.h"
#include "home.h"
#include "home_icons.h"
#include "home_logo.h"
#include "image.h"
#include "mesh3d.h"
#include "models.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HW 1920
#define HH 1080
#define OBJ_Y 560           /* their centres */
#define LABEL_Y 300
#define STATUS_Y 790
#define NOISE_W 128
#define NOISE_H 90
#define MAX_TRIS 1200
#define ICON 72            /* icon size on the 1920x1080 canvas */
#define DISC 104           /* the focus highlight behind an icon */

/* Where the objects stand (all three in view, as in the mockup): the chosen one comes
 * forward in its place. */
static const int SLOT_X[HOME_ITEMS] = { 630, 1140, 1600 };

static const SDL_Color WHITE = { 255, 255, 255, 255 };
static const SDL_Color DIM = { 150, 150, 190, 255 };
static const SDL_Color GREEN = { 120, 220, 140, 255 };
static const SDL_Color YELLOW = { 240, 210, 90, 255 };

static struct {
    SDL_Renderer *ren;
    SDL_Texture  *background, *basic[2], *noise, *label, *icons[2], *disc;
    mesh          models[HOME_ITEMS];
    mesh          pins[2];              /* the SID chip's two rows */
    uint32_t      noise_seed, noise_at;
    float         pos;                  /* carousel position, eased towards home_sel */
    Uint32        last;
    unsigned      snack_serial;
    Uint32        snack_at;
} H;

static SDL_Texture *texture(SDL_Renderer *ren, const uint32_t *px, int w, int h, int access)
{
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, access, w, h);
    if (t && px)
        SDL_UpdateTexture(t, NULL, px, w * 4);
    return t;
}

static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    int r = (int)(((a >> 16) & 255) + (((b >> 16) & 255) - (int)((a >> 16) & 255)) * t);
    int g = (int)(((a >> 8) & 255) + (((b >> 8) & 255) - (int)((a >> 8) & 255)) * t);
    int bl = (int)((a & 255) + ((int)(b & 255) - (int)(a & 255)) * t);
    return 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bl;
}

/* Dark navy fading to C64 blue a little above the middle, the Commodore rainbow on the
 * right (as on the PS5 tile's backgrounds), and the PS64 logo top left. */
static SDL_Texture *make_background(SDL_Renderer *ren)
{
    static const uint32_t rainbow[5] = { 0xE03A2F, 0xF08A24, 0xF5D02E, 0x3FA34D, 0x3B7DD8 };
    uint32_t *px = malloc((size_t)HW * HH * 4);
    if (!px)
        return NULL;
    for (int y = 0; y < HH; y++) {
        float t = (y / (float)HH - 0.47f) / 0.21f;
        float glow = expf(-t * t);
        for (int x = 0; x < HW; x++)
            px[y * HW + x] = mix(0xFF0B0926, 0xFF2E2C9B, glow * (1.0f - 0.3f * x / HW));
    }
    for (int i = 0; i < 5; i++)
        for (int y = 640 + i * 66; y < 640 + i * 66 + 50; y++)
            for (int x = 700; x < HW; x++)
                px[y * HW + x] = mix(px[y * HW + x], 0xFF000000u | rainbow[i], 0.42f);
    uint32_t *logo = NULL;
    int lw, lh, frames, *delays = NULL;
    if (image_decode(home_logo_png, sizeof(home_logo_png), &logo, &lw, &lh, &frames, &delays) == 0) {
        const int size = 176, lx = 80, ly = 74;
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
                px[(ly + y) * HW + lx + x] = logo[(y * lh / size) * lw + x * lw / size];
        free(logo);
        free(delays);
    }
    SDL_Texture *t = texture(ren, px, HW, HH, SDL_TEXTUREACCESS_STATIC);
    free(px);
    return t;
}

static void setup(SDL_Renderer *ren)
{
    if (H.ren == ren && H.background)
        return;
    home_cleanup();
    H.ren = ren;
    H.background = make_background(ren);
    uint32_t *px = malloc((size_t)HOME_C64_W * HOME_C64_H * 4);
    if (px) {
        for (int c = 0; c < 2; c++) {
            home_basic_screen(px, c);
            H.basic[c] = texture(ren, px, HOME_C64_W, HOME_C64_H, SDL_TEXTUREACCESS_STATIC);
        }
        free(px);
    }
    uint32_t label[HOME_LABEL_W * HOME_LABEL_H];
    home_chip_label(label);
    H.label = texture(ren, label, HOME_LABEL_W, HOME_LABEL_H, SDL_TEXTUREACCESS_STATIC);
    H.noise = texture(ren, NULL, NOISE_W, NOISE_H, SDL_TEXTUREACCESS_STREAMING);
    /* Power and settings icons from SVG, and the soft disc that marks the focused one. */
    static uint32_t icon[ICON * ICON], disc[DISC * DISC];
    const char *svgs[2] = { home_icon_power, home_icon_settings };
    for (int i = 0; i < 2; i++)
        if (home_icon(icon, ICON, svgs[i]) == 0) {
            H.icons[i] = texture(ren, icon, ICON, ICON, SDL_TEXTUREACCESS_STATIC);
            SDL_SetTextureBlendMode(H.icons[i], SDL_BLENDMODE_BLEND);
        }
    home_disc(disc, DISC);
    H.disc = texture(ren, disc, DISC, DISC, SDL_TEXTUREACCESS_STATIC);
    SDL_SetTextureBlendMode(H.disc, SDL_BLENDMODE_BLEND);
    model_1702(&H.models[HOME_VICE]);
    model_1084(&H.models[HOME_STREAM]);
    model_sid(&H.models[HOME_SID], &H.pins[0], &H.pins[1]);
}

void home_cleanup(void)
{
    SDL_Texture **all[] = { &H.background, &H.basic[0], &H.basic[1], &H.noise, &H.label,
                            &H.icons[0], &H.icons[1], &H.disc };
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
        if (*all[i]) {
            SDL_DestroyTexture(*all[i]);
            *all[i] = NULL;
        }
    for (int i = 0; i < HOME_ITEMS; i++)
        mesh_free(&H.models[i]);
    mesh_free(&H.pins[0]);
    mesh_free(&H.pins[1]);
    H.ren = NULL;
}

/* A mesh's projected triangles, in batches of the same texture. */
static void draw_mesh(SDL_Renderer *ren, const mesh *m, const pose3 *p, SDL_Texture *tex)
{
    static tri2 tris[MAX_TRIS];
    static SDL_Vertex verts[MAX_TRIS * 3];
    int n = mesh_prepare(m, p, tris, MAX_TRIS), nv = 0, batch_textured = -1;
    for (int i = 0; i <= n; i++) {
        int textured = i < n ? tris[i].textured && tex : -1;
        if (textured != batch_textured && nv) {
            SDL_RenderGeometry(ren, batch_textured ? tex : NULL, verts, nv, NULL, 0);
            nv = 0;
        }
        if (i == n)
            break;
        batch_textured = textured;
        const tri2 *t = &tris[i];
        for (int k = 0; k < 3; k++) {
            SDL_Vertex *v = &verts[nv++];
            v->position = (SDL_FPoint){ t->x[k], t->y[k] };
            v->color = (SDL_Color){ (Uint8)(t->rgb >> 16), (Uint8)(t->rgb >> 8), (Uint8)t->rgb, 255 };
            v->tex_coord = (SDL_FPoint){ t->u[k], t->v[k] };
        }
    }
}

static void draw_icons(SDL_Renderer *ren, const ui_state *u)
{
    const int xs[2] = { 1696, 1802 }, y = 96;
    for (int i = 0; i < 2; i++) {
        int focused = u->home_focus == (i ? HOME_ON_GEAR : HOME_ON_POWER);
        if (focused && H.disc) {
            SDL_SetTextureAlphaMod(H.disc, 56);
            SDL_Rect d = { xs[i] - DISC / 2, y - DISC / 2, DISC, DISC };
            SDL_RenderCopy(ren, H.disc, NULL, &d);
        }
        if (H.icons[i]) {
            SDL_Color c = focused ? WHITE : DIM;
            SDL_SetTextureColorMod(H.icons[i], c.r, c.g, c.b);
            SDL_Rect r = { xs[i] - ICON / 2, y - ICON / 2, ICON, ICON };
            SDL_RenderCopy(ren, H.icons[i], NULL, &r);
        }
    }
}

static void centred(SDL_Renderer *ren, const char *text, int cx, int y, int scale, SDL_Color c)
{
    font_draw(ren, text, cx - font_width(text, scale) / 2, y, scale, c);
}

static const char *stream_status(const ui_state *u, Uint32 now, SDL_Color *c, char *buf, size_t cap)
{
    *c = DIM;
    switch (u->home_stream) {
    case HOME_STREAM_LIVE:
        *c = GREEN;
        return "C64 Ultimate";
    case HOME_STREAM_CONNECTING:
        return "Connecting";
    case HOME_STREAM_LOCKED:
        *c = YELLOW;
        return "Password needed: see settings";
    case HOME_STREAM_NONE:
        return "No C64 Ultimate found";
    }
    snprintf(buf, cap, "Searching %c", "|/-\\"[(now / 150) % 4]);
    return buf;
}

static void draw_snack(SDL_Renderer *ren, const ui_state *u, Uint32 now)
{
    if (u->home_snack_serial != H.snack_serial) {
        H.snack_serial = u->home_snack_serial;
        H.snack_at = now;
    }
    if (!H.snack_at || now - H.snack_at > 2500)
        return;
    int w = font_width(u->home_snack, 3) + 64;
    fill_rect(ren, (HW - w) / 2, 940, w, 64, 20, 20, 32, 235);
    centred(ren, u->home_snack, HW / 2, 960, 3, WHITE);
}

void home_draw(SDL_Renderer *ren, const ui_state *u, SDL_Texture *live, Uint32 now)
{
    setup(ren);
    int saved_w, saved_h;
    SDL_RenderGetLogicalSize(ren, &saved_w, &saved_h);
    SDL_RenderSetLogicalSize(ren, HW, HH);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (H.background)
        SDL_RenderCopy(ren, H.background, NULL, NULL);

    /* Ease the carousel towards the chosen object. */
    float dt = H.last ? (now - H.last) / 1000.0f : 0;
    H.last = now;
    float k = dt * 7 > 1 ? 1 : dt * 7;
    H.pos += (u->home_sel - H.pos) * k;
    if (fabsf(u->home_sel - H.pos) < 0.001f)
        H.pos = (float)u->home_sel;

    /* TV static on the 1084 while there's no picture. */
    if (u->home_stream != HOME_STREAM_LIVE && H.noise && now - H.noise_at > 50) {
        static uint32_t px[NOISE_W * NOISE_H];
        home_noise(px, NOISE_W * NOISE_H, &H.noise_seed);
        SDL_UpdateTexture(H.noise, NULL, px, NOISE_W * 4);
        H.noise_at = now;
    }
    SDL_Texture *screens[HOME_ITEMS] = {
        H.basic[(now / 500) & 1],
        u->home_stream == HOME_STREAM_LIVE && live ? live : H.noise,
        H.label,
    };

    /* Far objects first: the chosen one comes forward and turns almost to face us. */
    int order[HOME_ITEMS] = { 0, 1, 2 };
    float d[HOME_ITEMS];
    for (int i = 0; i < HOME_ITEMS; i++)
        d[i] = fabsf(i - H.pos);
    for (int i = 0; i < HOME_ITEMS; i++)
        for (int j = i + 1; j < HOME_ITEMS; j++)
            if (d[order[j]] > d[order[i]]) {
                int t = order[i];
                order[i] = order[j];
                order[j] = t;
            }
    float t = now / 1000.0f;
    for (int n = 0; n < HOME_ITEMS; n++) {
        int i = order[n];
        float off = i - H.pos, near = 1 - fminf(1, fabsf(off));
        float side = off > 0 ? 1.0f : off < 0 ? -1.0f : 0;
        float sway = 0.06f * sinf(t * 0.9f + i * 2.1f);
        pose3 p = {
            .cx = (float)SLOT_X[i],
            .cy = OBJ_Y + 7 * sinf(t * 1.3f + i * 1.7f),
            .scale = 300 + 90 * near,
            .yaw = side * 0.50f * (1 - near) + 0.14f * near + sway,
            .pitch = -0.10f + 0.03f * sinf(t * 0.7f + i),
            .bright = 0.72f + 0.28f * near,
        };
        if (i == HOME_SID) {                    /* the chip lies down, seen from above */
            p.yaw += 0.55f;
            p.pitch = -0.80f + 0.04f * sinf(t * 0.7f);
            p.scale *= 0.8f;
        }
        if (i == HOME_SID) {
            float x, y, near_d, far_d;
            mesh_project(&p, (vec3){0, 0, -0.25f}, &x, &y, &near_d);
            mesh_project(&p, (vec3){0, 0, 0.25f}, &x, &y, &far_d);
            int back = near_d > far_d ? 0 : 1;
            draw_mesh(ren, &H.pins[back], &p, NULL);
            draw_mesh(ren, &H.models[i], &p, screens[i]);
            draw_mesh(ren, &H.pins[!back], &p, NULL);
        } else {
            draw_mesh(ren, &H.models[i], &p, screens[i]);
        }
    }

    static const char *const names[HOME_ITEMS] = { "Vice", "Stream", "SID" };
    for (int i = 0; i < HOME_ITEMS; i++) {
        int cx = SLOT_X[i];
        int on = i == u->home_sel && u->home_focus == HOME_ON_ITEMS;
        centred(ren, names[i], cx, LABEL_Y, 5, on ? WHITE : DIM);
        char buf[64];
        SDL_Color c = DIM;
        const char *status = i == HOME_STREAM ? stream_status(u, now, &c, buf, sizeof(buf))
                           : i == HOME_SID ? "Music player" : "Emulator";
        int sw = font_width(status, 3) + 32;
        fill_rect(ren, cx - sw / 2, STATUS_Y - 12, sw, 48, 11, 9, 38, 200);     /* readable over the stripes */
        centred(ren, status, cx, STATUS_Y, 3, c);
    }
    draw_icons(ren, u);
    centred(ren, u->home_focus == HOME_ON_ITEMS ? "X start     < > choose     up: settings, power"
                                                : "X select     < > choose     down: back",
            HW / 2, 1036, 2, DIM);
    draw_snack(ren, u, now);
    SDL_RenderSetLogicalSize(ren, saved_w, saved_h);
}
