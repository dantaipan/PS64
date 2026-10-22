#!/bin/sh
# Fetch the SID player's engine into third_party and build it (Mac, or PS5 in its container):
# libresidfp (the reSIDfp SID emulation) and libsidplayfp (the C64 around it),
# both GPL-2.0-or-later, from their pinned release tarballs.
#
#   tools/get_sidplayfp.sh          fetch (if needed) and build both into third_party/sidplay-osx
#   tools/get_sidplayfp.sh fetch    fetch and unpack only
#   tools/get_sidplayfp.sh ps5      (inside the PS5 build container) cross-build into third_party/sidplay-ps5
#
# The tarballs already carry their generated files (configure, the assembled PSID
# driver), so neither autotools nor the xa assembler is needed. VICE carries its own,
# older reSIDfp in the same C++ namespace, so this one is built as ps64_reSIDfp.
set -eu
cd "$(dirname "$0")/.."

fetch() {   # name version sha256 url
    dir=third_party/$1
    if [ "$(cat "$dir/.ps64-version" 2>/dev/null)" != "$2" ]; then
        rm -rf "$dir"
        mkdir -p "$dir"
        tmp=$(mktemp -d)
        curl -fsSL -o "$tmp/src.tar.gz" "$4"
        echo "$3  $tmp/src.tar.gz" | shasum -a 256 -c - >/dev/null
        tar -xzf "$tmp/src.tar.gz" -C "$dir" --strip-components 1
        rm -rf "$tmp"
        echo "$2" > "$dir/.ps64-version"
    fi
}

fetch libresidfp 1.2.3 e9b53a984ca708c3e8d9139eb6aad6141f73ffc22d70a020f7966803f4e2f76d \
      https://github.com/libsidplayfp/libresidfp/releases/download/v1.2.3/libresidfp-1.2.3.tar.gz
fetch libsidplayfp 3.1.1 12b79190593bf480b2d11481b5c2de62bac07f344437a66cd8d887329875c626 \
      https://github.com/libsidplayfp/libsidplayfp/releases/download/v3.1.1/libsidplayfp-3.1.1.tar.gz
[ "${1:-}" = fetch ] && exit 0

if [ "${1:-}" = ps5 ]; then
    SDK=${PS5_PAYLOAD_SDK:?run inside the PS5 build container}
    PLATFORM=ps5
    JOBS=$(nproc)
    set -- --host=x86_64-pc-freebsd CC="$SDK/bin/prospero-clang" CXX="$SDK/bin/prospero-clang++" \
           AR="$SDK/bin/prospero-ar" RANLIB="$SDK/bin/prospero-ranlib" CFLAGS="-O2"
else
    PLATFORM=osx
    JOBS=$(sysctl -n hw.ncpu)
    set --
fi
PREFIX=$(pwd)/third_party/sidplay-$PLATFORM
RENAME=-DreSIDfp=ps64_reSIDfp
build() {   # name, configure options...
    name=$1
    shift
    rm -rf "third_party/$name/build-$PLATFORM"
    mkdir -p "third_party/$name/build-$PLATFORM"
    (cd "third_party/$name/build-$PLATFORM" &&
     ../configure --prefix="$PREFIX" --disable-shared --enable-static \
                  --disable-dependency-tracking CXXFLAGS="-O2 $RENAME" "$@" >/dev/null &&
     make -j"$JOBS" >/dev/null && make install >/dev/null)
}
if [ ! -f "$PREFIX/lib/libsidplayfp.a" ] ||
   [ "$(cat "$PREFIX/.ps64-versions" 2>/dev/null)" != "1.2.3 3.1.1 renamed" ]; then
    rm -rf "$PREFIX"
    build libresidfp "$@"
    PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" \
    RESIDFP_CFLAGS="-I$PREFIX/include" RESIDFP_LIBS="-L$PREFIX/lib -lresidfp" \
        build libsidplayfp --without-exsid --without-usbsid "$@"
    echo "1.2.3 3.1.1 renamed" > "$PREFIX/.ps64-versions"
fi
echo "libresidfp 1.2.3, libsidplayfp 3.1.1: third_party/sidplay-$PLATFORM"
