#!/usr/bin/env python3
"""
Generate the PS5 launcher icon (512x512 PNG) in C64 colours:

  python3 tools/gen_icon.py ps5/icon0.png

Light-blue border, blue screen, "PS64" in a blocky 8x8 C64-style font and
the rainbow stripes. Pure standard library (zlib + struct).
"""
import struct
import sys
import zlib

W = H = 512
BORDER = (0x70, 0x6D, 0xEB)       # C64 light blue
SCREEN = (0x2E, 0x2C, 0x9B)       # C64 blue
TEXT = (0x70, 0x6D, 0xEB)
STRIPES = [(0xC4, 0x6C, 0x71), (0xED, 0xF1, 0x71), (0x56, 0xAC, 0x4D), (0x75, 0xCE, 0xC8)]

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
    for i, colour in enumerate(STRIPES):
        off = 300 + i * 28
        if off <= (x + (511 - y)) * 0.5 + 60 < off + 20 and y > 270:
            return colour
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
