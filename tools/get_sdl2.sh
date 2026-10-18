#!/bin/sh
# Download the official SDL 2.30.9 macOS framework into third_party/ for the Mac build.
set -e
cd "$(dirname "$0")/.."
VER=2.30.9
[ -d third_party/SDL2.framework ] && { echo "third_party/SDL2.framework already present"; exit 0; }
TMP=$(mktemp -d)
curl -sSL -o "$TMP/SDL2.dmg" "https://github.com/libsdl-org/SDL/releases/download/release-$VER/SDL2-$VER.dmg"
hdiutil attach -nobrowse -readonly -mountpoint "$TMP/mnt" "$TMP/SDL2.dmg" >/dev/null
mkdir -p third_party
cp -R "$TMP/mnt/SDL2.framework" third_party/
hdiutil detach "$TMP/mnt" >/dev/null
rm -rf "$TMP"
echo "SDL2 $VER installed in third_party/SDL2.framework"
