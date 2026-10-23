#!/bin/sh
# Package the Mac build as PS64.app in a disk image: dist/PS64-<version>.dmg.
#
#   tools/make_dmg.sh
#
# The app carries SDL2.framework and HVSC's song lengths; its settings and the folders
# you fill (games, sids, carts) are in ~/Documents/PS64. Apple Silicon only (VICE and
# libsidplayfp are built for arm64). Signed ad hoc: on another Mac, right-click PS64 >
# Open the first time (or allow it in System Settings > Privacy & Security).
set -eu
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^#define PS64_VERSION "\(.*\)"/\1/p' src/version.h)
[ -n "$VERSION" ] || { echo "make_dmg: no PS64_VERSION in src/version.h" >&2; exit 1; }
[ -f third_party/hvsc/Songlengths.md5 ] || { echo "make_dmg: run tools/get_songlengths.sh first" >&2; exit 1; }
[ -f third_party/vice/vice_x64sc_libretro_osx.a ] || { echo "make_dmg: run tools/get_vice.sh first" >&2; exit 1; }
[ -f third_party/sidplay-osx/lib/libsidplayfp.a ] || { echo "make_dmg: run tools/get_sidplayfp.sh first" >&2; exit 1; }
[ -d third_party/SDL2.framework ] || { echo "make_dmg: run tools/get_sdl2.sh first" >&2; exit 1; }

make ps64

STAGE=dist/stage
APP=$STAGE/PS64.app
DMG=dist/PS64-$VERSION.dmg
rm -rf "$STAGE" "$DMG"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"

cp ps64 "$APP/Contents/MacOS/ps64"
ditto third_party/SDL2.framework "$APP/Contents/Frameworks/SDL2.framework"
cp third_party/hvsc/Songlengths.md5 "$APP/Contents/Resources/"
install_name_tool -add_rpath @executable_path/../Frameworks "$APP/Contents/MacOS/ps64"

# The icon: the PS5 tile's, at the sizes macOS wants.
ICONSET=dist/PS64.iconset
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
    sips -z $s $s ps5/native/sce_sys/icon0.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    d=$((s * 2))
    [ $d -le 512 ] && sips -z $d $d ps5/native/sce_sys/icon0.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/PS64.icns"
rm -rf "$ICONSET"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>                  <string>PS64</string>
    <key>CFBundleDisplayName</key>           <string>PS64</string>
    <key>CFBundleIdentifier</key>            <string>net.dantaipan.ps64</string>
    <key>CFBundleExecutable</key>            <string>ps64</string>
    <key>CFBundleIconFile</key>              <string>PS64</string>
    <key>CFBundlePackageType</key>           <string>APPL</string>
    <key>CFBundleShortVersionString</key>    <string>$VERSION</string>
    <key>CFBundleVersion</key>               <string>$VERSION</string>
    <key>LSMinimumSystemVersion</key>        <string>11.0</string>
    <key>NSHighResolutionCapable</key>       <true/>
    <key>NSLocalNetworkUsageDescription</key>
    <string>PS64 finds your C64 Ultimate on the local network and receives its picture and sound.</string>
    <key>NSHumanReadableCopyright</key>      <string>GPL-3.0; VICE GPL-2.0-or-later</string>
</dict>
</plist>
EOF

codesign --force --deep -s - "$APP"
codesign --verify --deep --strict "$APP"

ln -s /Applications "$STAGE/Applications"
hdiutil create -quiet -volname PS64 -srcfolder "$STAGE" -format UDZO "$DMG"
rm -rf "$STAGE"
echo "made $DMG ($(du -h "$DMG" | cut -f1))"
