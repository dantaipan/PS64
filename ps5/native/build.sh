#!/bin/sh
# Build PS64 as a native PS5 app (title PPSA06464, for ShadowMountPlus and the
# Prospero Store), in Docker. Output: ps5/native/out/PPSA06464/ and PPSA06464.zip.
#
#   ps5/native/build.sh                 build
#   ps5/native/build.sh deploy <ps5-ip> build and copy to /data/homebrew/PPSA06464 (FTP 2121)
#
# Uses the PS5 Native App Boilerplate (pinned) for linking, the FSELF and packaging,
# PS64's SDL2 with the patches in sdl-ps5-native.patch, and dlmalloc.
set -e
cd "$(dirname "$0")/../.."
PATH="$PATH:/Applications/Docker.app/Contents/Resources/bin"

BOILERPLATE_REPO=https://github.com/blackbearreloaded/ps5-native-app-boilerplate.git
BOILERPLATE_COMMIT=44ae1dab529654eac795d2a7c4f5a0425102b8d7
SDL_REPO=https://github.com/ps5-payload-dev/SDL.git
SDL_COMMIT=ee4c47dc0
DLMALLOC_URL=https://gee.cs.oswego.edu/pub/misc/malloc.c
DLMALLOC_SHA256=be7be56b40b4b31417e74e77687f9b96127ea6cae534cf236aee40a4c739508d

# Fetch a pinned commit into dir (once).
fetch() {
    dir=$1 repo=$2 commit=$3
    [ -f "$dir/.ps64-commit" ] && [ "$(cat "$dir/.ps64-commit")" = "$commit" ] && return 0
    rm -rf "$dir"
    git clone -q "$repo" "$dir"
    git -C "$dir" checkout -q "$commit"
    echo "$commit" > "$dir/.ps64-commit"
    echo "fetched $repo @ $commit"
}

tools/get_vice.sh fetch
fetch third_party/ps5-native-app-boilerplate "$BOILERPLATE_REPO" "$BOILERPLATE_COMMIT"
# Two small changes to the boilerplate (boilerplate.patch): the linker searches the SDK's
# libraries (the C++ runtime asks for libpthread by name), and the linker script marks the
# exception tables (__eh_frame_*) for the C++ unwinder.
if git -C third_party/ps5-native-app-boilerplate diff --quiet; then
    git -C third_party/ps5-native-app-boilerplate apply "$PWD/ps5/native/boilerplate.patch"
fi
if [ ! -f third_party/sdl-native/.ps64-commit ] || [ "$(cat third_party/sdl-native/.ps64-commit)" != "$SDL_COMMIT" ]; then
    fetch third_party/sdl-native "$SDL_REPO" "$SDL_COMMIT"
    git -C third_party/sdl-native apply "$PWD/ps5/native/sdl-ps5-native.patch"
    rm -rf third_party/sdl-native-build
fi
if [ ! -f third_party/dlmalloc/dlmalloc.c ]; then
    mkdir -p third_party/dlmalloc
    curl -sSL -o third_party/dlmalloc/dlmalloc.c "$DLMALLOC_URL"
fi
echo "$DLMALLOC_SHA256  third_party/dlmalloc/dlmalloc.c" | shasum -a 256 -c - >/dev/null || {
    echo "dlmalloc.c doesn't match the pinned SHA-256"; exit 1; }

docker image inspect ps64-ps5-sdk >/dev/null 2>&1 || docker build -t ps64-ps5-sdk ps5
docker image inspect ps64-native >/dev/null 2>&1 || docker build -t ps64-native ps5/native

docker run --rm -v "$PWD":/src -w /src ps64-native bash ps5/native/inside.sh "$@"
