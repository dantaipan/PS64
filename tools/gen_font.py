#!/usr/bin/env python3
"""
Generate src/font8x8.h for the on-screen keyboard and menus.

  python3 tools/gen_font.py path/to/font8x8_basic.h src/font8x8.h

0x20-0x7E  ASCII from font8x8_basic by Daniel Hepper (public domain,
           https://github.com/dhepper/font8x8)
0x80-0x85  C64 key symbols: pound, up arrow, left arrow, up/down, left/right; star
0xA0-0xDF  PETSCII-style graphics, indexed by PETSCII code (upper-case/graphics
           set), hand-drawn look-alikes of the C64 characters

Rows are drawn left to right with '#' for a set pixel; bit 0 of each byte is
the leftmost pixel.
"""
import re
import sys

SYMBOLS = {
    0x80: ("pound", ["..###...", ".#...#..", ".#......", "####....",
                     ".#......", ".#....#.", "######..", "........"]),
    0x81: ("up arrow", ["...#....", "..###...", ".#.#.#..", "...#....",
                        "...#....", "...#....", "...#....", "........"]),
    0x82: ("left arrow", ["........", "..#.....", ".#......", "#######.",
                          ".#......", "..#.....", "........", "........"]),
    0x83: ("up/down", ["...#....", "..###...", ".#.#.#..", "...#....",
                       ".#.#.#..", "..###...", "...#....", "........"]),
    0x84: ("left/right", ["........", "..#..#..", ".#....#.", "########",
                          ".#....#.", "..#..#..", "........", "........"]),
    0x85: ("star", ["...#....", "...#....", "#######.", ".#####..",
                    "..###...", ".##.##..", ".#...#..", "........"]),
}

E = "........"
F = "########"

# PETSCII 0xA0-0xBF (C= keys, mostly blocks and box pieces).
PETSCII_A0 = [
    ("shift space", [E] * 8),
    ("left half block", ["####...."] * 8),
    ("lower half block", [E] * 4 + [F] * 4),
    ("top line", [F] + [E] * 7),
    ("bottom line", [E] * 7 + [F]),
    ("left line", ["#......."] * 8),
    ("checkerboard", ["#.#.#.#.", ".#.#.#.#"] * 4),
    ("right line", [".......#"] * 8),
    ("lower checkerboard", [E] * 4 + ["#.#.#.#.", ".#.#.#.#"] * 2),
    ("upper-left triangle", ["########", "#######.", "######..", "#####...",
                             "####....", "###.....", "##......", "#......."]),
    ("right quarter bar", ["......##"] * 8),
    ("tee right", ["...##..."] * 3 + ["...#####"] * 2 + ["...##..."] * 3),
    ("lower-right quarter", [E] * 4 + ["....####"] * 4),
    ("corner up-right", ["...##..."] * 3 + ["...#####"] * 2 + [E] * 3),
    ("corner left-down", [E] * 3 + ["#####..."] * 2 + ["...##..."] * 3),
    ("lower quarter block", [E] * 6 + [F] * 2),
    ("corner right-down", [E] * 3 + ["...#####"] * 2 + ["...##..."] * 3),
    ("tee up", ["...##..."] * 3 + [F] * 2 + [E] * 3),
    ("tee down", [E] * 3 + [F] * 2 + ["...##..."] * 3),
    ("tee left", ["...##..."] * 3 + ["#####..."] * 2 + ["...##..."] * 3),
    ("left quarter bar", ["##......"] * 8),
    ("left three-eighths", ["###....."] * 8),
    ("right three-eighths", [".....###"] * 8),
    ("top quarter block", [F] * 2 + [E] * 6),
    ("top three-eighths", [F] * 3 + [E] * 5),
    ("bottom three-eighths", [E] * 5 + [F] * 3),
    ("bottom-right corner lines", [".......#"] * 7 + [F]),
    ("lower-left quarter", [E] * 4 + ["####...."] * 4),
    ("upper-right quarter", ["....####"] * 4 + [E] * 4),
    ("corner up-left", ["...##..."] * 3 + ["#####..."] * 2 + [E] * 3),
    ("upper-left quarter", ["####...."] * 4 + [E] * 4),
    ("diagonal quarters", ["####...."] * 4 + ["....####"] * 4),
]

# PETSCII 0xC0-0xDF (shifted keys: lines, suits, curves).
PETSCII_C0 = [
    ("horizontal line", [E] * 3 + [F] + [E] * 4),
    ("spade", ["...#....", "..###...", ".#####..", "#######.",
               "#######.", "...#....", "..###...", "........"]),
    ("vertical line", ["...#...."] * 8),
    ("horizontal line high", [E] * 2 + [F] + [E] * 5),
    ("horizontal line upper", [E] + [F] + [E] * 6),
    ("horizontal line top", [F] + [E] * 7),
    ("horizontal line low", [E] * 5 + [F] + [E] * 2),
    ("vertical line left", ["..#....."] * 8),
    ("vertical line right", ["....#..."] * 8),
    ("curve left-down", [E] * 3 + ["###....."] + ["...#...."] + ["...#...."] * 3),
    ("curve up-right", ["...#...."] * 3 + ["...#...."] + [".....###"] + [E] * 3),
    ("curve up-left", ["...#...."] * 3 + ["..#....."] + ["##......"] + [E] * 3),
    ("bottom-left corner lines", ["#......."] * 7 + [F]),
    ("diagonal down", ["#.......", ".#......", "..#.....", "...#....",
                       "....#...", ".....#..", "......#.", ".......#"]),
    ("diagonal up", [".......#", "......#.", ".....#..", "....#...",
                     "...#....", "..#.....", ".#......", "#......."]),
    ("top-left corner lines", [F] + ["#......."] * 7),
    ("top-right corner lines", [F] + [".......#"] * 7),
    ("filled circle", ["........", "..###...", ".#####..", ".#####..",
                       ".#####..", "..###...", "........", "........"]),
    ("horizontal line bottom", [E] * 6 + [F] + [E]),
    ("heart", [".##.##..", "#######.", "#######.", "#######.",
               ".#####..", "..###...", "...#....", "........"]),
    ("vertical line far left", [".#......"] * 8),
    ("curve right-down", [E] * 3 + ["....####"] + ["...#...."] + ["...#...."] * 3),
    ("diagonal cross", ["##....##", "###..###", ".######.", "..####..",
                        "..####..", ".######.", "###..###", "##....##"]),
    ("circle", ["........", "..###...", ".#...#..", ".#...#..",
                ".#...#..", "..###...", "........", "........"]),
    ("club", ["...##...", "..####..", "...##...", ".##..##.",
              "########", ".##..##.", "...##...", "..####.."]),
    ("vertical line far right", ["......#."] * 8),
    ("diamond", ["...#....", "..###...", ".#####..", "#######.",
                 ".#####..", "..###...", "...#....", "........"]),
    ("cross", ["...#...."] * 3 + [F] + ["...#...."] * 4),
    ("left checkerboard", ["#.#.....", ".#.#....", "#.#.....", ".#.#....",
                           "#.#.....", ".#.#....", "#.#.....", ".#.#...."]),
    ("vertical line centre", ["...##..."] * 8),
    ("pi", [E, F, ".##..##.", ".##..##.", ".##..##.", ".##..##.", ".##..###", E]),
    ("upper-right triangle", ["########", ".#######", "..######", "...#####",
                              "....####", ".....###", "......##", ".......#"]),
]


def enc(pic):
    assert len(pic) == 8 and all(len(r) == 8 for r in pic), pic
    return [sum(1 << i for i, c in enumerate(row) if c == "#") for row in pic]


def main():
    src, dst = sys.argv[1], sys.argv[2]
    rows = re.findall(r"\{\s*((?:0x[0-9A-Fa-f]{2},\s*){7}0x[0-9A-Fa-f]{2})\s*\}",
                      open(src).read())
    assert len(rows) == 128, "expected font8x8_basic with 128 glyphs"
    ascii_glyphs = [[int(x, 16) for x in r.split(",")] for r in rows]
    assert len(PETSCII_A0) == 32 and len(PETSCII_C0) == 32

    glyphs = {}
    for c in range(0x20, 0x7F):
        name = chr(c)
        name = {"\\": "backslash", "*": "star", "/": "slash", "'": "quote"}.get(name, "'%s'" % name)
        glyphs[c] = (ascii_glyphs[c], name)
    for code, (name, pic) in SYMBOLS.items():
        glyphs[code] = (enc(pic), name)
    for i, (name, pic) in enumerate(PETSCII_A0):
        glyphs[0xA0 + i] = (enc(pic), "PETSCII $%02X %s" % (0xA0 + i, name))
    for i, (name, pic) in enumerate(PETSCII_C0):
        glyphs[0xC0 + i] = (enc(pic), "PETSCII $%02X %s" % (0xC0 + i, name))

    out = [
        "/*",
        " * 8x8 font for the on-screen keyboard and menus. Generated by",
        " * tools/gen_font.py - edit the pixel art there, not this file.",
        " *",
        " *   0x20-0x7E  ASCII from font8x8_basic by Daniel Hepper (public domain,",
        " *              based on public-domain IBM VGA fonts)",
        " *   0x80-0x85  C64 key symbols (pound, arrows) and a star",
        " *   0xA0-0xDF  PETSCII-style graphics, by PETSCII code",
        " *",
        " * Bit 0 of each row is the leftmost pixel.",
        " */",
        "#ifndef FONT8X8_H",
        "#define FONT8X8_H",
        "",
        "#define FONT_FIRST 0x20",
        "#define FONT_LAST  0xDF",
        "",
        "static const unsigned char font8x8[FONT_LAST - FONT_FIRST + 1][8] = {",
    ]
    for c in range(0x20, 0xE0):
        g, name = glyphs.get(c, ([0] * 8, "unused"))
        out.append("    { %s },  /* 0x%02X %s */" % (", ".join("0x%02X" % b for b in g), c, name))
    out += ["};", "", "#endif", ""]
    open(dst, "w").write("\n".join(out))


if __name__ == "__main__":
    main()
