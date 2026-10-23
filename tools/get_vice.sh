#!/bin/sh
# Fetch the VICE C64 emulator (libretro core, x64sc) into third_party/vice and
# build it as a static library for PS64's built-in emulator mode.
#
#   tools/get_vice.sh          fetch + build for the Mac (third_party/vice/vice_x64sc_libretro_osx.a)
#   tools/get_vice.sh fetch    fetch only (ps5/build.sh builds the PS5 library in Docker)
#
# Source: mihawk-99/PS5_VICE, a fork of libretro/vice-libretro (VICE 3.10) with a
# PS5 platform, pinned to the commit PS64 was tested with. VICE is GPL-2.0-or-later.
set -e
cd "$(dirname "$0")/.."
REPO=https://github.com/mihawk-99/PS5_VICE.git
COMMIT=983b9882e8606975646dda07d6331d51df5d43ac
DIR=third_party/vice

if [ ! -f "$DIR/.ps64-commit" ] || [ "$(cat "$DIR/.ps64-commit")" != "$COMMIT" ]; then
    rm -rf "$DIR"
    mkdir -p "$DIR"
    git -C "$DIR" init -q
    git -C "$DIR" remote add origin "$REPO"
    git -C "$DIR" fetch -q --depth 1 origin "$COMMIT"
    git -C "$DIR" checkout -q FETCH_HEAD
    # Its old zlib takes macOS for classic Mac OS (and then wants <unix.h>).
    perl -pi -e 's/^#if defined\(MACOS\) \|\| defined\(TARGET_OS_MAC\)$/#if defined(MACOS)/' \
        "$DIR/deps/libz/zutil.h"
    echo "$COMMIT" > "$DIR/.ps64-commit"
    echo "VICE source fetched into $DIR"
fi

# Autostart waits for READY. on the line above the cursor; a cartridge can print a line
# under it (Epyx FastLoad: "FASTLOAD"), so READY. one line higher counts too.
if ! grep -q "PS64: a cartridge" "$DIR/vice/src/autostart.c"; then
    perl -0pi -e 's/(static CHECKYESNO check\(const char \*s, unsigned int blink_mode\)\n\{\n)    return check2\(s, blink_mode, 0, AUTOSTART_CHECK_FIRST_COLUMN\);\n/$1    CHECKYESNO r = check2(s, blink_mode, 0, AUTOSTART_CHECK_FIRST_COLUMN);\n    \/\* PS64: a cartridge may print a line under READY. (Epyx FastLoad: "FASTLOAD") \*\/\n    if (r == NO \&\& blink_mode == AUTOSTART_WAIT_BLINK \&\&\n        check2(s, blink_mode, -1, AUTOSTART_CHECK_FIRST_COLUMN) == YES)\n        return YES;\n    return r;\n/' \
        "$DIR/vice/src/autostart.c"
    grep -q "PS64: a cartridge" "$DIR/vice/src/autostart.c" || { echo "get_vice: autostart patch didn't apply" >&2; exit 1; }
    rm -f "$DIR"/vice_x64sc_libretro_*.a        # rebuild with it
fi

[ "$1" = fetch ] && exit 0

JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
make -C "$DIR" -j"$JOBS" platform=osx EMUTYPE=x64sc STATIC_LINKING=1 \
     OBJDIR=build-osx TARGET=vice_x64sc_libretro_osx.a
echo "VICE built: $DIR/vice_x64sc_libretro_osx.a"
