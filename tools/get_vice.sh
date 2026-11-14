#!/bin/sh
# Fetch the VICE C64 emulator (libretro core, x64sc) into third_party/vice and
# build it as a static library for PS64's built-in emulator mode.
#
#   tools/get_vice.sh          fetch + build for the Mac: the C64 core
#                              (third_party/vice/vice_x64sc_libretro_osx.a) and the VIC-20 core
#                              (vice_xvic_osx.a: entry points renamed vic20_* by
#                              tools/vice_vic20_names.h, everything else made local, so the two
#                              link into one program)
#                              and the SuperCPU core (vice_xscpu64_osx.a, scpu_*, the same way)
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
    rm -f "$DIR"/vice_*.a                       # rebuild with it
fi

# JiffyDOS takes all its ROMs from system/vice, and a drive whose ROM isn't there doesn't
# work at all; people often have only some (no 1571's), so a missing one is the stock ROM.
if ! grep -q "PS64: a JiffyDOS ROM" "$DIR/retrodep/ui.c"; then
    perl -0pi -e '
        my %stock = ("1541-II" => "DRIVE_ROM1541II_NAME", "1571_repl310654" => "DRIVE_ROM1571_NAME",
                     "1581" => "DRIVE_ROM1581_NAME", "C64" => "C64_KERNAL_REV3_NAME",
                     "SX-64" => "C64_KERNAL_SX64_NAME", "C128" => "C128_KERNAL_NAME");
        s/("JiffyDOS_([^"]+)\.bin"\);\n\s*log_resources_set_string\("\w+", )\(const char\*\)tmp_str\)/$1ps64_rom(tmp_str, $stock{$2}))/g;
        s/(int ui_init_finalize\(void\)\n)/\/* PS64: a JiffyDOS ROM that isn\x27t there: the stock one. *\/\nstatic const char *ps64_rom(const char *path, const char *stock)\n{\n   return util_file_exists(path) ? path : stock;\n}\n\n$1/;
    ' "$DIR/retrodep/ui.c"
    [ "$(grep -c 'ps64_rom(' "$DIR/retrodep/ui.c")" -ge 7 ] || { echo "get_vice: JiffyDOS patch didn't apply" >&2; exit 1; }
    rm -f "$DIR"/vice_*.a                       # rebuild with it
fi

# The SuperCPU core's names and keep list: the VIC-20's with scpu_ for vic20_ (one list to
# keep up to date). Written into dir; ps5/Makefile uses them too.
scpu_names() {   # (rewritten only when they change, so builds don't redo the core)
    sed -e 's/vic20_/scpu_/g' -e 's/PS64_VIC20_NAMES_H/PS64_SCPU_NAMES_H/' \
        -e 's/VIC-20 core (xvic)/SuperCPU core (xscpu64)/' tools/vice_vic20_names.h > "$1/scpu_names.h.new"
    sed 's/vic20_/scpu_/' tools/vice_vic20_keep.txt > "$1/scpu-keep.txt.new"
    for f in scpu_names.h scpu-keep.txt; do
        if cmp -s "$1/$f.new" "$1/$f"; then rm "$1/$f.new"; else mv "$1/$f.new" "$1/$f"; fi
    done
}
scpu_names "$DIR"

# A renamed core built before the name list changed is built again from scratch (the
# forced header isn't a dependency of its objects).
renamed_core_stale() {   # library, object folder
    if [ tools/vice_vic20_names.h -nt "$DIR/$1" ] || [ tools/vice_vic20_keep.txt -nt "$DIR/$1" ]; then
        rm -rf "$DIR/$1" "$DIR/$2"
    fi
}

[ "$1" = fetch ] && exit 0

JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
make -C "$DIR" -j"$JOBS" platform=osx EMUTYPE=x64sc STATIC_LINKING=1 \
     OBJDIR=build-osx TARGET=vice_x64sc_libretro_osx.a
echo "VICE built: $DIR/vice_x64sc_libretro_osx.a"

# The VIC-20 core. -fno-common makes its uninitialised globals real definitions, so the
# partial link can make them local too.
renamed_core_stale vice_xvic_osx.a build-osx-xvic
if [ ! -f "$DIR/vice_xvic_osx.a" ]; then
    CFLAGS="-fno-common -include $PWD/tools/vice_vic20_names.h" \
    make -C "$DIR" -j"$JOBS" platform=osx EMUTYPE=xvic STATIC_LINKING=1 \
         OBJDIR=build-osx-xvic TARGET=vice_xvic_libretro_osx.a
    sed 's/^/_/' tools/vice_vic20_keep.txt > "$DIR/vic20-keep-macho.txt"
    ld -r -arch arm64 -platform_version macos 11.0 11.0 -all_load "$DIR/vice_xvic_libretro_osx.a" \
       -exported_symbols_list "$DIR/vic20-keep-macho.txt" -o "$DIR/vice_xvic_osx.o" 2>/dev/null
    rm -f "$DIR/vice_xvic_osx.a"
    ar rcs "$DIR/vice_xvic_osx.a" "$DIR/vice_xvic_osx.o"
    if nm -g "$DIR/vice_xvic_osx.o" | grep -v " U " | awk '{print $3}' | grep -qv '^_vic20_'; then
        echo "get_vice: the VIC-20 core still has other global symbols" >&2
        exit 1
    fi
fi
echo "VICE built: $DIR/vice_xvic_osx.a"

# The SuperCPU core (xscpu64), linked the same way with an scpu_ prefix.
renamed_core_stale vice_xscpu64_osx.a build-osx-xscpu64
if [ ! -f "$DIR/vice_xscpu64_osx.a" ]; then
    CFLAGS="-fno-common -include $PWD/$DIR/scpu_names.h" \
    make -C "$DIR" -j"$JOBS" platform=osx EMUTYPE=xscpu64 STATIC_LINKING=1 \
         OBJDIR=build-osx-xscpu64 TARGET=vice_xscpu64_libretro_osx.a
    sed 's/^/_/' "$DIR/scpu-keep.txt" > "$DIR/scpu-keep-macho.txt"
    ld -r -arch arm64 -platform_version macos 11.0 11.0 -all_load "$DIR/vice_xscpu64_libretro_osx.a" \
       -exported_symbols_list "$DIR/scpu-keep-macho.txt" -o "$DIR/vice_xscpu64_osx.o" 2>/dev/null
    rm -f "$DIR/vice_xscpu64_osx.a"
    ar rcs "$DIR/vice_xscpu64_osx.a" "$DIR/vice_xscpu64_osx.o"
    if nm -g "$DIR/vice_xscpu64_osx.o" | grep -v " U " | awk '{print $3}' | grep -qv '^_scpu_'; then
        echo "get_vice: the SuperCPU core still has other global symbols" >&2
        exit 1
    fi
fi
echo "VICE built: $DIR/vice_xscpu64_osx.a"
