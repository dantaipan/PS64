/*
 * c64keys - the C64 keyboard as data: matrix-ordered key ids, short labels,
 * Ultimate REST input names, and PETSCII codes read from the KERNAL's own
 * decode tables. No networking, no SDL.
 */
#ifndef C64KEYS_H
#define C64KEYS_H

/* Key ids are C64 keyboard-matrix codes (0-63), plus RESTORE. */
enum {
    C64K_INST_DEL = 0, C64K_RETURN = 1, C64K_CRSR_LR = 2, C64K_F7 = 3,
    C64K_F1 = 4, C64K_F3 = 5, C64K_F5 = 6, C64K_CRSR_UD = 7,
    C64K_LSHIFT = 15, C64K_CLR_HOME = 51, C64K_RSHIFT = 52,
    C64K_CTRL = 58, C64K_SPACE = 60, C64K_CBM = 61, C64K_RUN_STOP = 63,
    C64K_RESTORE = 64,
    C64K_COUNT = 65
};

/* Modifier bits. */
#define C64MOD_SHIFT 1
#define C64MOD_CBM   2
#define C64MOD_CTRL  4

/* REST input-command name ("a", "run_stop", ...). */
const char *c64key_api_name(int key);

/* Short label for the on-screen keyboard; shifted F-keys read F2/F4/F6/F8. */
const char *c64key_label(int key, int mods);

/* Shift, C= and CTRL themselves (they modify, they don't type). */
int c64key_is_modifier(int key);

/* PETSCII the KERNAL would put in the keyboard buffer, or -1 if none. */
int c64key_petscii(int key, int mods);

/*
 * REST input names for key+mods: modifier keys first, then the key.
 * Returns the count written to names (max 4), 0 if not sendable.
 */
int c64key_api_inputs(int key, int mods, const char *names[4]);

/*
 * ASCII -> key+mods that types it. Letters of either case use the plain
 * key (capitals in the C64's default character set); '\n' is RETURN.
 * Returns 0, or -1 if no key types that character.
 */
int c64key_from_ascii(int ch, int *key, int *mods);

/*
 * Parse a key name for config files: REST names, plus aliases such as
 * f2/f4/f6/f8, cursor_up/down/left/right, home, clr, del, inst, run_stop.
 * Returns 0 and sets key/mods, or -1 if unknown.
 */
int c64key_parse(const char *name, int *key, int *mods);

#endif
