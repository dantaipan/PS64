#include "osk.h"

#include "c64keys.h"

#include <stdlib.h>

/* C64 layout; the F-key column sits to the right at x = 66. */
static const osk_key row0[] = {
    {57, 0,4},{56, 4,4},{59, 8,4},{ 8,12,4},{11,16,4},{16,20,4},{19,24,4},{24,28,4},
    {27,32,4},{32,36,4},{35,40,4},{40,44,4},{43,48,4},{48,52,4},{51,56,4},{ 0,60,4},
    { 4,66,6},
};
static const osk_key row1[] = {
    {58, 0,6},{62, 6,4},{ 9,10,4},{14,14,4},{17,18,4},{22,22,4},{25,26,4},{30,30,4},
    {33,34,4},{38,38,4},{41,42,4},{46,46,4},{49,50,4},{54,54,4},{64,58,6},
    { 5,66,6},
};
static const osk_key row2[] = {
    {63, 0,6},{10, 6,4},{13,10,4},{18,14,4},{21,18,4},{26,22,4},{29,26,4},{34,30,4},
    {37,34,4},{42,38,4},{45,42,4},{50,46,4},{53,50,4},{ 1,54,10},
    { 6,66,6},
};
static const osk_key row3[] = {
    {61, 0,4},{15, 4,6},{12,10,4},{23,14,4},{20,18,4},{31,22,4},{28,26,4},{39,30,4},
    {36,34,4},{47,38,4},{44,42,4},{55,46,4},{52,50,6},{ 7,56,4},{ 2,60,4},
    { 3,66,6},
};
static const osk_key row4[] = {
    {60,14,36},
};

static const struct { const osk_key *keys; int len; } rows[OSK_ROWS] = {
    { row0, sizeof(row0) / sizeof(row0[0]) },
    { row1, sizeof(row1) / sizeof(row1[0]) },
    { row2, sizeof(row2) / sizeof(row2[0]) },
    { row3, sizeof(row3) / sizeof(row3[0]) },
    { row4, sizeof(row4) / sizeof(row4[0]) },
};

int osk_row_len(int row)
{
    return row >= 0 && row < OSK_ROWS ? rows[row].len : 0;
}

const osk_key *osk_key_at(int row, int col)
{
    if (col < 0 || col >= osk_row_len(row))
        return NULL;
    return &rows[row].keys[col];
}

void osk_init(osk_state *s)
{
    s->open = 0;
    s->row = 2;
    s->col = 1;          /* "A" */
    s->shift = s->cbm = s->ctrl = 0;
}

void osk_move(osk_state *s, int dx, int dy)
{
    if (dx) {
        int len = osk_row_len(s->row);
        s->col = ((s->col + dx) % len + len) % len;
    }
    if (dy) {
        /* Keep the horizontal position: pick the key nearest the current centre. */
        const osk_key *cur = osk_key_at(s->row, s->col);
        int centre = cur->x * 2 + cur->w;
        s->row = ((s->row + dy) % OSK_ROWS + OSK_ROWS) % OSK_ROWS;
        int best = 0, best_dist = 1 << 30;
        for (int c = 0; c < osk_row_len(s->row); c++) {
            const osk_key *k = osk_key_at(s->row, c);
            int d = abs(k->x * 2 + k->w - centre);
            if (d < best_dist) {
                best_dist = d;
                best = c;
            }
        }
        s->col = best;
    }
}

int osk_mods(const osk_state *s)
{
    return (s->shift ? C64MOD_SHIFT : 0) | (s->cbm ? C64MOD_CBM : 0) | (s->ctrl ? C64MOD_CTRL : 0);
}

void osk_toggle_shift(osk_state *s) { s->shift = (s->shift + 1) % 3; }
void osk_toggle_cbm(osk_state *s)   { s->cbm = !s->cbm; }
void osk_toggle_ctrl(osk_state *s)  { s->ctrl = !s->ctrl; }

int osk_press(osk_state *s, int *key, int *mods)
{
    int k = osk_key_at(s->row, s->col)->key;
    switch (k) {
    case C64K_LSHIFT: case C64K_RSHIFT: osk_toggle_shift(s); return 0;
    case C64K_CBM:                      osk_toggle_cbm(s);   return 0;
    case C64K_CTRL:                     osk_toggle_ctrl(s);  return 0;
    }
    *key = k;
    *mods = osk_mods(s);
    if (s->shift == 1)
        s->shift = 0;
    s->cbm = s->ctrl = 0;
    return 1;
}
