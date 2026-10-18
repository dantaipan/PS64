#!/bin/sh
# Copy ps5/out/PS64 to /data/homebrew/PS64 on a jailbroken PS5 over FTP
# (ftpsrv payload), then load the websrv homebrew launcher via elfldr.
#
#   ps5/deploy.sh <ps5-ip>            upload and load websrv
#   ps5/deploy.sh <ps5-ip> --no-websrv  upload only
#
# Env overrides: FTP_PORT (default 2121), ELFLDR_PORT (default 9021).
set -e
cd "$(dirname "$0")"
PS5="${1:?usage: $0 <ps5-ip> [--no-websrv]}"
FTP_PORT="${FTP_PORT:-2121}"
ELFLDR_PORT="${ELFLDR_PORT:-9021}"
APP=out/PS64
DEST="ftp://$PS5:$FTP_PORT/data/homebrew/PS64"

[ -f "$APP/eboot.elf" ] || { echo "build first: ps5/build.sh"; exit 1; }

echo "Uploading to $DEST ..."
curl -sS --ftp-create-dirs -T "$APP/eboot.elf"          "$DEST/eboot.elf"
curl -sS --ftp-create-dirs -T "$APP/sce_sys/icon0.png"  "$DEST/sce_sys/icon0.png"
# Keep an existing config on the PS5 so local edits survive redeploys.
if curl -sS -o /dev/null "$DEST/ps64.cfg" 2>/dev/null; then
    echo "Keeping existing ps64.cfg on the PS5"
else
    curl -sS --ftp-create-dirs -T "$APP/ps64.cfg" "$DEST/ps64.cfg"
fi

echo "Uploaded."

# The app was called C64View before: carry its settings, search index,
# favourites and recents over to the new folder and names, then remove the
# old folder so the launcher doesn't list both.
OLD="ftp://$PS5:$FTP_PORT/data/homebrew/C64View"
if curl -sS -o /dev/null "$OLD/eboot.elf" 2>/dev/null; then
    echo "Moving settings from the old C64View folder ..."
    TMP=$(mktemp -d)
    ok=1
    for pair in c64view.cfg:ps64.cfg c64view-index.txt:ps64-index.txt \
                c64view-favourites.txt:ps64-favourites.txt c64view-recents.txt:ps64-recents.txt; do
        old=${pair%%:*}; new=${pair##*:}
        if curl -sS -o "$TMP/$old" "$OLD/$old" 2>/dev/null; then
            # The fresh install above may have put a default ps64.cfg there; the old one wins.
            curl -sS -T "$TMP/$old" "$DEST/$new" && echo "  $old -> $new" || ok=0
        fi
    done
    rm -rf "$TMP"
    if [ $ok = 1 ]; then
        HOST="ftp://$PS5:$FTP_PORT"
        D=/data/homebrew/C64View
        for f in eboot.elf sce_sys/icon0.png c64view.cfg c64view-index.txt \
                 c64view-favourites.txt c64view-recents.txt INSTALL.txt; do
            curl -sS -o /dev/null -Q "DELE $D/$f" "$HOST/" 2>/dev/null || true
        done
        curl -sS -o /dev/null -Q "RMD $D/sce_sys" "$HOST/" 2>/dev/null || true
        if curl -sS -o /dev/null -Q "RMD $D" "$HOST/" 2>/dev/null; then
            echo "  removed the old C64View folder"
        else
            echo "  (the old C64View folder still has other files; remove it by hand if you like)"
        fi
    fi
fi

if [ "$2" != "--no-websrv" ]; then
    if nc -z -w 2 "$PS5" 8080 2>/dev/null; then
        echo "websrv is already running."
    elif nc -z -w 2 "$PS5" "$ELFLDR_PORT" 2>/dev/null; then
        echo "Loading websrv via elfldr on port $ELFLDR_PORT ..."
        curl -sSL https://github.com/ps5-payload-dev/websrv/releases/latest/download/websrv-ps5.elf \
            | nc -w 2 "$PS5" "$ELFLDR_PORT"
    else
        echo "No ELF loader on port $ELFLDR_PORT, so websrv can't be sent from here."
        echo "Start websrv on the PS5 with your payload manager (or add it to autoload), then rerun."
        exit 1
    fi
    echo "Open http://$PS5:8080/ (PS5 browser or this Mac) and launch PS64."
fi
