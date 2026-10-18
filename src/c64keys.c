#include "c64keys.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

/* Matrix order, as used by both the KERNAL tables and the REST name list. */
static const char *const api_names[C64K_COUNT] = {
    "inst_del", "return", "cursor_left_right", "f7", "f1", "f3", "f5", "cursor_up_down",
    "3", "w", "a", "4", "z", "s", "e", "left_shift",
    "5", "r", "d", "6", "c", "f", "t", "x",
    "7", "y", "g", "8", "b", "h", "u", "v",
    "9", "i", "j", "0", "m", "k", "o", "n",
    "plus", "p", "l", "minus", "period", "colon", "at", "comma",
    "pound", "star", "semicolon", "clr_home", "right_shift", "equals", "arrow_up", "slash",
    "1", "arrow_left", "ctrl", "2", "space", "commodore", "q", "run_stop",
    "restore",
};

/* Labels use the overlay font: \x80 pound, \x81 up arrow, \x82 left arrow,
 * \x83 up/down arrow, \x84 left/right arrow. */
static const char *const labels[C64K_COUNT] = {
    "DEL", "RETURN", "CRS\x84", "F7", "F1", "F3", "F5", "CRS\x83",
    "3", "W", "A", "4", "Z", "S", "E", "SHIFT",
    "5", "R", "D", "6", "C", "F", "T", "X",
    "7", "Y", "G", "8", "B", "H", "U", "V",
    "9", "I", "J", "0", "M", "K", "O", "N",
    "+", "P", "L", "-", ".", ":", "@", ",",
    "\x80", "*", ";", "HOME", "SHIFT", "=", "\x81", "/",
    "1", "\x82", "CTRL", "2", "SPACE", "C=", "Q", "RUN/S",
    "RESTR",
};

/*
 * KERNAL keyboard decode tables, read from a C64 Ultimate ($EB81 plain,
 * $EBC2 shift, $EC03 C=, $EC78 CTRL). $FF = no character. The modifier keys
 * hold flag values (1/2/4) that c64key_petscii filters out.
 */
static const unsigned char tab_plain[64] = {
    0x14,0x0d,0x1d,0x88,0x85,0x86,0x87,0x11, 0x33,0x57,0x41,0x34,0x5a,0x53,0x45,0x01,
    0x35,0x52,0x44,0x36,0x43,0x46,0x54,0x58, 0x37,0x59,0x47,0x38,0x42,0x48,0x55,0x56,
    0x39,0x49,0x4a,0x30,0x4d,0x4b,0x4f,0x4e, 0x2b,0x50,0x4c,0x2d,0x2e,0x3a,0x40,0x2c,
    0x5c,0x2a,0x3b,0x13,0x01,0x3d,0x5e,0x2f, 0x31,0x5f,0x04,0x32,0x20,0x02,0x51,0x03,
};
static const unsigned char tab_shift[64] = {
    0x94,0x8d,0x9d,0x8c,0x89,0x8a,0x8b,0x91, 0x23,0xd7,0xc1,0x24,0xda,0xd3,0xc5,0x01,
    0x25,0xd2,0xc4,0x26,0xc3,0xc6,0xd4,0xd8, 0x27,0xd9,0xc7,0x28,0xc2,0xc8,0xd5,0xd6,
    0x29,0xc9,0xca,0x30,0xcd,0xcb,0xcf,0xce, 0xdb,0xd0,0xcc,0xdd,0x3e,0x5b,0xba,0x3c,
    0xa9,0xc0,0x5d,0x93,0x01,0x3d,0xde,0x3f, 0x21,0x5f,0x04,0x22,0xa0,0x02,0xd1,0x83,
};
static const unsigned char tab_cbm[64] = {
    0x94,0x8d,0x9d,0x8c,0x89,0x8a,0x8b,0x91, 0x96,0xb3,0xb0,0x97,0xad,0xae,0xb1,0x01,
    0x98,0xb2,0xac,0x99,0xbc,0xbb,0xa3,0xbd, 0x9a,0xb7,0xa5,0x9b,0xbf,0xb4,0xb8,0xbe,
    0x29,0xa2,0xb5,0x30,0xa7,0xa1,0xb9,0xaa, 0xa6,0xaf,0xb6,0xdc,0x3e,0x5b,0xa4,0x3c,
    0xa8,0xdf,0x5d,0x93,0x01,0x3d,0xde,0x3f, 0x81,0x5f,0x04,0x95,0xa0,0x02,0xab,0x83,
};
static const unsigned char tab_ctrl[64] = {
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0x1c,0x17,0x01,0x9f,0x1a,0x13,0x05,0xff,
    0x9c,0x12,0x04,0x1e,0x03,0x06,0x14,0x18, 0x1f,0x19,0x07,0x9e,0x02,0x08,0x15,0x16,
    0x12,0x09,0x0a,0x92,0x0d,0x0b,0x0f,0x0e, 0xff,0x10,0x0c,0xff,0xff,0x1b,0x00,0xff,
    0x1c,0xff,0x1d,0xff,0xff,0x1f,0x1e,0xff, 0x90,0x06,0xff,0x05,0xff,0xff,0x11,0xff,
};

static int valid(int key) { return key >= 0 && key < C64K_COUNT; }

const char *c64key_api_name(int key)
{
    return valid(key) ? api_names[key] : NULL;
}

/* Colour and reverse-video control codes, as typed with CTRL or C= + digit. */
static const struct { unsigned char code; const char *name; } control_names[] = {
    {0x90,"BLK"},{0x05,"WHT"},{0x1C,"RED"},{0x9F,"CYN"},{0x9C,"PUR"},{0x1E,"GRN"},
    {0x1F,"BLU"},{0x9E,"YEL"},{0x81,"ORG"},{0x95,"BRN"},{0x96,"LRED"},{0x97,"GRY1"},
    {0x98,"GRY2"},{0x99,"LGRN"},{0x9A,"LBLU"},{0x9B,"GRY3"},{0x12,"RVS+"},{0x92,"RVS-"},
};

const char *c64key_label(int key, int mods)
{
    if (!valid(key))
        return "";
    if (mods & C64MOD_SHIFT) {
        switch (key) {
        case C64K_F1: return "F2";
        case C64K_F3: return "F4";
        case C64K_F5: return "F6";
        case C64K_F7: return "F8";
        case C64K_INST_DEL: return "INST";
        case C64K_CLR_HOME: return "CLR";
        case C64K_RUN_STOP: return "LOAD";
        }
    }
    if (!mods || key >= 64 || c64key_is_modifier(key))
        return labels[key];

    /* Show what the key types with these modifiers: the character itself,
     * a PETSCII graphics glyph (the font has them at their PETSCII codes),
     * or a colour name. Anything else keeps the plain label. */
    int p = c64key_petscii(key, mods);
    static char single[256][2];
    if ((p >= 0x20 && p <= 0x5D && p != 0x5C) || (p >= 0xA1 && p <= 0xDF)) {
        single[p][0] = (char)p;
        return single[p];
    }
    switch (p) {
    case 0x5C: return "\x80";       /* pound */
    case 0x5E: return "\x81";       /* up arrow */
    case 0x5F: return "\x82";       /* left arrow */
    }
    for (size_t i = 0; i < sizeof(control_names) / sizeof(control_names[0]); i++)
        if (control_names[i].code == p)
            return control_names[i].name;
    return labels[key];
}

int c64key_is_modifier(int key)
{
    return key == C64K_LSHIFT || key == C64K_RSHIFT || key == C64K_CBM || key == C64K_CTRL;
}

int c64key_petscii(int key, int mods)
{
    if (key < 0 || key >= 64 || c64key_is_modifier(key))
        return -1;
    unsigned char c;
    if (mods & C64MOD_CTRL)       c = tab_ctrl[key];
    else if (mods & C64MOD_CBM)   c = tab_cbm[key];
    else if (mods & C64MOD_SHIFT) c = tab_shift[key];
    else                          c = tab_plain[key];
    return c == 0xff ? -1 : c;
}

int c64key_api_inputs(int key, int mods, const char *names[4])
{
    if (!valid(key) || c64key_is_modifier(key))
        return 0;
    if (key == C64K_RESTORE) {          /* must be sent alone */
        names[0] = api_names[key];
        return 1;
    }
    int n = 0;
    if (mods & C64MOD_SHIFT) names[n++] = api_names[C64K_LSHIFT];
    if (mods & C64MOD_CBM)   names[n++] = api_names[C64K_CBM];
    if (mods & C64MOD_CTRL)  names[n++] = api_names[C64K_CTRL];
    names[n++] = api_names[key];
    return n;
}

static const struct { const char *name; int key, mods; } aliases[] = {
    { "f2", C64K_F1, C64MOD_SHIFT }, { "f4", C64K_F3, C64MOD_SHIFT },
    { "f6", C64K_F5, C64MOD_SHIFT }, { "f8", C64K_F7, C64MOD_SHIFT },
    { "cursor_down", C64K_CRSR_UD, 0 }, { "cursor_up", C64K_CRSR_UD, C64MOD_SHIFT },
    { "cursor_right", C64K_CRSR_LR, 0 }, { "cursor_left", C64K_CRSR_LR, C64MOD_SHIFT },
    { "home", C64K_CLR_HOME, 0 }, { "clr", C64K_CLR_HOME, C64MOD_SHIFT },
    { "del", C64K_INST_DEL, 0 }, { "inst", C64K_INST_DEL, C64MOD_SHIFT },
    { "runstop", C64K_RUN_STOP, 0 }, { "shift_run_stop", C64K_RUN_STOP, C64MOD_SHIFT },
};

int c64key_parse(const char *name, int *key, int *mods)
{
    for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++)
        if (!strcasecmp(name, aliases[i].name)) {
            *key = aliases[i].key;
            *mods = aliases[i].mods;
            return 0;
        }
    for (int k = 0; k < C64K_COUNT; k++)
        if (!strcasecmp(name, api_names[k])) {
            *key = k;
            *mods = 0;
            return 0;
        }
    return -1;
}

int c64key_from_ascii(int ch, int *key, int *mods)
{
    if (ch == '\n' || ch == '\r') {
        *key = C64K_RETURN;
        *mods = 0;
        return 0;
    }
    int want = isalpha(ch) ? toupper(ch) : ch;
    static const int try_mods[2] = { 0, C64MOD_SHIFT };
    for (int m = 0; m < 2; m++)
        for (int k = 0; k < 64; k++)
            if (c64key_petscii(k, try_mods[m]) == want) {
                *key = k;
                *mods = try_mods[m];
                return 0;
            }
    return -1;
}
