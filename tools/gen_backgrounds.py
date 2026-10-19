#!/usr/bin/env python3
"""
Generate the PS5 home-screen backgrounds for the native app:

  python3 tools/gen_backgrounds.py OUT_DIR [--preview preview.png]

Writes OUT_DIR/pic0.dds (shown while the PS64 tile is selected) and pic1.dds
(while it launches): a C64-blue screen with the Commodore rainbow, red at the
top to blue at the bottom, running from the middle to the right edge, so the
left side stays clear for the PS5's title and Play button.

The format is the one the native app boilerplate checks: 3840x2160,
BC7_UNORM in a DX10 DDS, one mip level. Every stripe edge is on a multiple of
4 pixels, so each 4x4 block is a single colour and is written as a solid BC7
block (mode 6, all indices 0). Pure standard library.
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_icon import RAINBOW, SCREEN  # noqa: E402  (same colours as the icon)

W, H = 3840, 2160
STRIPE_X = 1920                  # stripes run from here to the right edge
STRIPE_H = 120
STRIPE_TOP = 1000                # a little below the middle, like the badge


def colour(x, y):
    if x >= STRIPE_X and STRIPE_TOP <= y < STRIPE_TOP + len(RAINBOW) * STRIPE_H:
        return RAINBOW[(y - STRIPE_TOP) // STRIPE_H]
    return SCREEN


def bc7_solid(rgb):
    """A BC7 mode-6 block of one colour: endpoint 0 = the colour, every index 0."""
    def seven(v):                # v ~= (c7 << 1) | 1, the p-bit is 1 (alpha 255)
        return max(0, min(127, (v - 1 + 1) // 2))
    bits, pos = 0, 0

    def put(value, count):
        nonlocal bits, pos
        bits |= value << pos
        pos += count
    put(1 << 6, 7)               # mode 6
    r, g, b = (seven(c) for c in rgb)
    for c in (r, g, b, 127):     # R0 R1 G0 G1 B0 B1 A0 A1
        put(c, 7)
        put(c, 7)
    put(1, 1)                    # P0
    put(1, 1)                    # P1
    put(0, 63)                   # indices
    return bits.to_bytes(16, "little")


def dds(path):
    blocks_w, blocks_h = W // 4, H // 4
    cache = {}
    body = bytearray()
    for by in range(blocks_h):
        for bx in range(blocks_w):
            c = colour(bx * 4, by * 4)
            if c not in cache:
                cache[c] = bc7_solid(c)
            body += cache[c]
    pixel_format = struct.pack("<II4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
    header = struct.pack("<7I44s32s5I", 124, 0x81007, H, W, len(body), 0, 1, b"\0" * 44,
                         pixel_format, 0x1000, 0, 0, 0, 0)
    dx10 = struct.pack("<5I", 98, 3, 0, 1, 0)       # BC7_UNORM, 2D texture, one surface
    with open(path, "wb") as f:
        f.write(b"DDS " + header + dx10 + body)


def preview(path, scale=8):
    w, h = W // scale, H // scale
    rows = b"".join(b"\0" + bytes(c for x in range(w) for c in colour(x * scale, y * scale))
                    for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    dds(os.path.join(out, "pic0.dds"))
    dds(os.path.join(out, "pic1.dds"))
    if len(sys.argv) == 4 and sys.argv[2] == "--preview":
        preview(sys.argv[3])


if __name__ == "__main__":
    main()
