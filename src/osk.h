/*
 * osk - on-screen C64 keyboard: layout, cursor movement and modifier state.
 * Pure logic; drawing lives in osk_sdl.c.
 */
#ifndef OSK_H
#define OSK_H

#define OSK_ROWS   5
#define OSK_WIDTH  72      /* layout width in quarter-key units */

typedef struct {
    int key;               /* c64keys id */
    int x, w;              /* quarter-key units */
} osk_key;

typedef struct {
    int open;
    int row, col;
    int shift;             /* 0 off, 1 next key only, 2 locked */
    int cbm, ctrl;         /* next key only */
} osk_state;

int  osk_row_len(int row);
const osk_key *osk_key_at(int row, int col);

void osk_init(osk_state *s);
void osk_move(osk_state *s, int dx, int dy);
int  osk_mods(const osk_state *s);          /* C64MOD_* bits */
void osk_toggle_shift(osk_state *s);        /* off -> once -> locked -> off */
void osk_toggle_cbm(osk_state *s);
void osk_toggle_ctrl(osk_state *s);

/*
 * Activate the highlighted key. Modifier keys toggle their state and return 0;
 * anything else returns 1 with the key and modifiers to send, then clears the
 * one-shot modifiers.
 */
int  osk_press(osk_state *s, int *key, int *mods);

#endif
