"""
Embed a file in the app as a C byte array:

  python3 tools/embed.py FILE NAME OUT.h

Writes `static const unsigned char NAME[]` with the file's bytes. Used for the
home screen's logo: python3 tools/embed.py ps5/native/sce_sys/icon0.png
home_logo_png src/home_logo.h (run again when the icon changes).
"""
import os
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    src, name, out = sys.argv[1:4]
    data = open(src, "rb").read()
    lines = [", ".join(f"0x{b:02X}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    guard = os.path.basename(out).upper().replace(".", "_")
    with open(out, "w") as f:
        f.write(f"/* {os.path.basename(src)}, embedded by tools/embed.py - don't edit. */\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write(f"static const unsigned char {name}[{len(data)}] = {{\n")
        f.write("".join(f"    {line},\n" for line in lines))
        f.write("};\n\n#endif\n")


if __name__ == "__main__":
    main()
