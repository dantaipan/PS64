#include "osk_sdl.h"

#include "c64keys.h"
#include "font_sdl.h"
#include "prompts_sdl.h"

#include <string.h>

#define MARGIN 12
#define KEY_H  34
#define GAP    4

void osk_draw(SDL_Renderer *ren, const osk_state *s, int hints)
{
    int saved_w, saved_h;
    canvas_begin(ren, &saved_w, &saved_h);

    const int span = CANVAS_W - 2 * MARGIN;          /* pixels for OSK_WIDTH quarters */
    const int panel_h = 22 + OSK_ROWS * (KEY_H + GAP) + 6;
    const int panel_y = CANVAS_H - panel_h;
    fill_rect(ren, 0, panel_y, CANVAS_W, panel_h, 0x10, 0x10, 0x20, 240);

    const SDL_Color dim = { 150, 150, 150, 255 }, lit = { 0xED, 0xF1, 0x71, 255 },
                    white = { 255, 255, 255, 255 }, text = { 220, 220, 220, 255 };
    int mods = osk_mods(s);
    font_draw(ren, s->shift == 2 ? "SHIFT LOCK" : "SHIFT", MARGIN, panel_y + 7, 1,
              s->shift ? lit : dim);
    font_draw(ren, "C=", MARGIN + 96, panel_y + 7, 1, s->cbm ? lit : dim);
    font_draw(ren, "CTRL", MARGIN + 128, panel_y + 7, 1, s->ctrl ? lit : dim);
    if (hints)
        prompts_draw(ren, "{cross} type {circle} close {square} del {triangle} space {r2} return "
                          "{l1} shift {r1} C= {l2} ctrl", MARGIN + 176, panel_y + 7, 1, dim);

    for (int r = 0; r < OSK_ROWS; r++) {
        for (int c = 0; c < osk_row_len(r); c++) {
            const osk_key *k = osk_key_at(r, c);
            int x = MARGIN + k->x * span / OSK_WIDTH;
            int y = panel_y + 22 + r * (KEY_H + GAP);
            int w = k->w * span / OSK_WIDTH - GAP;
            int selected = r == s->row && c == s->col;
            int active = c64key_is_modifier(k->key) &&
                         (((k->key == C64K_LSHIFT || k->key == C64K_RSHIFT) && s->shift) ||
                          (k->key == C64K_CBM && s->cbm) || (k->key == C64K_CTRL && s->ctrl));
            if (selected)
                fill_rect(ren, x, y, w, KEY_H, 0x70, 0x6D, 0xEB, 255);   /* C64 light blue */
            else if (active)
                fill_rect(ren, x, y, w, KEY_H, 0x8E, 0x50, 0x29, 255);
            else
                fill_rect(ren, x, y, w, KEY_H, 0x4A, 0x4A, 0x4A, 255);

            /* Labels show what the key types with the current modifiers;
             * single characters (incl. PETSCII graphics) are drawn double size. */
            const char *label = c64key_label(k->key, mods);
            int scale = strlen(label) == 1 ? 2 : 1;
            font_draw(ren, label, x + (w - font_width(label, scale)) / 2,
                      y + (KEY_H - 8 * scale) / 2, scale, selected || active ? white : text);
        }
    }
    canvas_end(ren, saved_w, saved_h);
}

void osk_draw_banner(SDL_Renderer *ren, const char *text)
{
    int saved_w, saved_h;
    canvas_begin(ren, &saved_w, &saved_h);
    int tw = font_width(text, 1);
    fill_rect(ren, (CANVAS_W - tw) / 2 - 10, 10, tw + 20, 24, 0, 0, 0, 200);
    font_draw(ren, text, (CANVAS_W - tw) / 2, 18, 1, (SDL_Color){ 255, 255, 255, 255 });
    canvas_end(ren, saved_w, saved_h);
}
