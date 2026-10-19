#!/usr/bin/env python3
"""
Generate the PS5 launcher icon (512x512 PNG) in C64 colours:

  python3 tools/gen_icon.py ps5/icon0.png

Light-blue border, blue screen, "PS64" in a blocky 8x8 C64-style font and
the Commodore rainbow: horizontal stripes, red at the top to blue at the
bottom, as on the machine. Pure standard library (zlib + struct).
"""
import struct
import sys
import zlib

W = H = 512
BORDER = (0x70, 0x6D, 0xEB)       # C64 light blue
SCREEN = (0x2E, 0x2C, 0x9B)       # C64 blue
TEXT = (0x70, 0x6D, 0xEB)
# The rainbow, top to bottom (also used by gen_backgrounds.py). The blue is lighter than
# the C64-blue screen so it stays visible on it.
RAINBOW = [(0xE0, 0x3A, 0x2F), (0xF0, 0x8A, 0x24), (0xF5, 0xD0, 0x2E), (0x3F, 0xA3, 0x4D),
           (0x3B, 0x7D, 0xD8)]
STRIPE_TOP, STRIPE_H = 286, 28    # five stripes from STRIPE_X to the screen's right edge,
STRIPE_GAP, STRIPE_X = 2, 72      # with a little blue between them and on their left

GLYPHS = {
    "P": ["#####...", "##..##..", "##..##..", "#####...", "##......", "##......", "##......", "........"],
    "S": [".####...", "##..##..", "##......", ".####...", "....##..", "##..##..", ".####...", "........"],
    "6": [".####...", "##..##..", "##......", "#####...", "##..##..", "##..##..", ".####...", "........"],
    "4": ["....##..", "...###..", "..####..", ".##.##..", "#######.", "....##..", "....##..", "........"],
}
TEXT_STR = "PS64"
SCALE = 12                        # 8x8 glyph -> 96x96 pixels
TEXT_Y = 100


def pixel(x, y):
    if x < 40 or x >= 472 or y < 40 or y >= 472:
        return BORDER
    width = len(TEXT_STR) * 8 * SCALE
    tx = (W - width) // 2
    if TEXT_Y <= y < TEXT_Y + 8 * SCALE and tx <= x < tx + width:
        ch = TEXT_STR[(x - tx) // (8 * SCALE)]
        gx = ((x - tx) % (8 * SCALE)) // SCALE
        gy = (y - TEXT_Y) // SCALE
        if GLYPHS[ch][gy][gx] == "#":
            return TEXT
    pitch = STRIPE_H + STRIPE_GAP
    if x >= STRIPE_X and STRIPE_TOP <= y < STRIPE_TOP + len(RAINBOW) * pitch:
        i, within = divmod(y - STRIPE_TOP, pitch)
        if within < STRIPE_H:
            return RAINBOW[i]
    return SCREEN


def main():
    rows = b"".join(b"\0" + bytes(c for x in range(W) for c in pixel(x, y)) for y in range(H))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
    open(sys.argv[1], "wb").write(png)


if __name__ == "__main__":
    main()
