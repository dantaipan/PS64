#!/bin/sh
# Fetch HVSC's song-length database (DOCUMENTS/Songlengths.md5, ~5 MB) into
# third_party/hvsc for the SID player: tunes end on time and the playlist moves on.
# PS64 can't fetch it itself (the HVSC sites are HTTPS only), so the native app
# carries it in its assets (/app0/assets/Songlengths.md5) and the Mac build reads
# it from third_party/hvsc. Pinned by SHA-256; HVSC updates it with each release
# (then update SHA256 here).
set -eu
cd "$(dirname "$0")/.."

SHA256=3f2bb5c14e3396fb20979cb017421386dc9e32162999974ad1e4f0a230dc397e
URL=https://hvsc.c64.org/download/C64Music/DOCUMENTS/Songlengths.md5
OUT=third_party/hvsc/Songlengths.md5

if ! echo "$SHA256  $OUT" | shasum -a 256 -c - >/dev/null 2>&1; then
    mkdir -p "$(dirname "$OUT")"
    curl -fsSL -o "$OUT.tmp" "$URL"
    if ! echo "$SHA256  $OUT.tmp" | shasum -a 256 -c - >/dev/null 2>&1; then
        echo "get_songlengths: $URL changed (a new HVSC release?) - check it and update SHA256" >&2
        echo "  new SHA-256: $(shasum -a 256 "$OUT.tmp" | cut -d' ' -f1)" >&2
        rm -f "$OUT.tmp"
        exit 1
    fi
    mv "$OUT.tmp" "$OUT"
fi
echo "song lengths: $OUT"
