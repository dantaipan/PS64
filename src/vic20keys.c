#include "vic20keys.h"

/*
 * The VIC-20 has the C64's keys, wired into its 8x8 matrix differently. Made from VICE's
 * positional keymaps (data/C64/sdl_pos.vkm and data/VIC20/sdl_pos.vkm): the same host key
 * gives a C64 position and a VIC-20 one. Index: C64 row * 8 + column; value: VIC-20's.
 */
static const unsigned char to_vic20[64] = {
    56, 57, 58, 63, 60, 61, 62, 59,
     8,  9, 10, 15, 12, 13, 14, 11,
    16, 17, 18, 23, 20, 21, 22, 19,
    24, 25, 26, 31, 28, 29, 30, 27,
    32, 33, 34, 39, 36, 37, 38, 35,
    40, 41, 42, 47, 44, 45, 46, 43,
    48, 49, 50, 55, 52, 53, 54, 51,
     0,  1,  2,  7,  4,  5,  6,  3,
};

int vic20_key(int c64key)
{
    return c64key >= 0 && c64key < 64 ? to_vic20[c64key] : -1;
}
