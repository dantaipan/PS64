#!/bin/bash
# Make a Prospero Store release of PS64 (see docs/RELEASING.md):
#
#   ps5/native/release.sh 01.000.001 notes.md          ask before publishing
#   ps5/native/release.sh 01.000.001 notes.md --yes    don't ask
#   ps5/native/release.sh 01.000.001 notes.md --dry-run  everything but publishing (no commit,
#                                                       tag or release; version files restored)
#
# VERSION is the new contentVersion (NN.NNN.NNN); notes.md becomes the release notes on
# prosperostore.net and in the store (write it for players, what changed first).
#
# Steps: check everything is ready; set the version in ps5/native/sce_sys/param.json and
# src/version.h; run the tests; build PPSA06464.zip; then (after you confirm) commit, tag,
# push, and publish the GitHub release with the zip. Finally prints the SHA-256 and writes
# the catalog record to ps5/native/out/PPSA06464.json for the catalog pull request.
set -euo pipefail
cd "$(dirname "$0")/../.."
PATH="$PATH:$HOME/.local/bin"

die() { echo "release: $*" >&2; exit 1; }

VERSION=${1:-}
NOTES=${2:-}
YES=${3:-}
[[ $VERSION =~ ^[0-9]{2}\.[0-9]{3}\.[0-9]{3}$ ]] ||
    die "usage: $0 NN.NNN.NNN notes.md [--yes|--dry-run]  (e.g. 01.000.001)"
[[ -s $NOTES ]] || die "release notes file missing or empty: $NOTES"

PARAM=ps5/native/sce_sys/param.json
REPO=dantaipan/PS64
HUMAN=$(python3 -c 'import sys; print(".".join(str(int(p)) for p in sys.argv[1].split(".")))' "$VERSION")
CURRENT=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["contentVersion"])' "$PARAM")

# ---- checks ----------------------------------------------------------------------------
command -v gh >/dev/null || die "the GitHub CLI (gh) is needed"
[[ -z $(git status --porcelain) ]] || die "commit or stash your changes first"
[[ $(git rev-parse --abbrev-ref HEAD) == main ]] || die "release from the main branch"
git fetch -q --tags origin
git rev-parse -q --verify "refs/tags/$VERSION" >/dev/null && die "tag $VERSION already exists"
# Higher than the current version, or equal for the very first release (no tag for it yet).
python3 - "$CURRENT" "$VERSION" <<'PY' || die "$VERSION isn't higher than the current $CURRENT"
import sys
cur, new = (tuple(int(p) for p in v.split(".")) for v in sys.argv[1:3])
sys.exit(0 if new >= cur else 1)
PY
if [[ $VERSION == "$CURRENT" ]] && git rev-parse -q --verify "refs/tags/$CURRENT" >/dev/null; then
    die "$VERSION is already released: raise the version"
fi
VIS=$(gh repo view "$REPO" --json visibility -q .visibility)
if [[ $VIS != PUBLIC ]]; then
    MSG="the repository is $VIS: the catalog needs it public (gh repo edit $REPO --visibility public --accept-visibility-change-consequences)"
    [[ $YES == --dry-run ]] && echo "release: (dry run) $MSG" || die "$MSG"
fi

# ---- version, tests, build -------------------------------------------------------------
restore() { git checkout -q -- "$PARAM" src/version.h; }
python3 - "$PARAM" "$VERSION" <<'PY'
import json, sys
p, v = sys.argv[1:3]
d = json.load(open(p))
d["contentVersion"] = v
with open(p, "w") as f:
    json.dump(d, f, indent=2)
    f.write("\n")
PY
sed -i '' "s/^#define PS64_VERSION \".*\"/#define PS64_VERSION \"$HUMAN\"/" src/version.h
grep -q "PS64_VERSION \"$HUMAN\"" src/version.h || { restore; die "couldn't set src/version.h"; }

echo "== tests"
make -s test >/dev/null || { restore; die "make test failed"; }
echo "== building PPSA06464.zip"
ps5/native/build.sh >/dev/null || { restore; die "native build failed"; }
ZIP=ps5/native/out/PPSA06464.zip
unzip -p "$ZIP" PPSA06464/sce_sys/param.json | grep -q "\"contentVersion\": \"$VERSION\"" ||
    { restore; die "the zip doesn't carry contentVersion $VERSION"; }
SHA=$(shasum -a 256 "$ZIP" | cut -d' ' -f1)

# ---- confirm, then publish -------------------------------------------------------------
echo
echo "Release PS64 $HUMAN (contentVersion $VERSION)"
echo "  zip     $ZIP ($(du -h "$ZIP" | cut -f1), sha256 $SHA)"
echo "  notes   $NOTES"
echo "  then    commit + tag $VERSION, push to $REPO, publish the GitHub release"
if [[ $YES == --dry-run ]]; then
    restore
    echo "Dry run: nothing published; version files restored. The zip is in $ZIP."
    exit 0
fi
if [[ $YES != --yes ]]; then
    read -r -p "Publish? [y/N] " answer
    [[ $answer == [yY]* ]] || { restore; echo "Nothing published; version files restored."; exit 1; }
fi

git add "$PARAM" src/version.h
git commit -q -m "Release PS64 $HUMAN ($VERSION)"
git tag -a "$VERSION" -m "PS64 $HUMAN"
git push -q origin main
git push -q origin "$VERSION"
gh release create "$VERSION" "$ZIP" -R "$REPO" --title "PS64 $HUMAN" --notes-file "$NOTES"

# ---- catalog record ----------------------------------------------------------------------
RECORD=ps5/native/out/PPSA06464.json
python3 - "$RECORD" "$VERSION" "$SHA" "$REPO" <<'PY'
import json, sys
path, version, sha, repo = sys.argv[1:5]
record = {
    "titleid": "PPSA06464",
    "name": "PS64",
    "kind": "app",
    "category": "emulators",
    "description": "Play the Commodore 64 on PS5: stream and control a C64 Ultimate, "
                   "or use the built-in VICE emulator, with Assembly64 search.",
    "license": "GPL-3.0",
    "author": "dantaipan",
    "version": version,
    "source_repo": f"https://github.com/{repo}",
    "artifact_url": f"https://github.com/{repo}/releases/download/{version}/PPSA06464.zip",
    "sha256": sha,
    "icon_url": f"https://raw.githubusercontent.com/{repo}/{version}/ps5/native/sce_sys/icon0.png",
}
with open(path, "w") as f:
    json.dump(record, f, indent=2)
    f.write("\n")
PY
echo
echo "Published: https://github.com/$REPO/releases/tag/$VERSION"
echo "Catalog record: $RECORD"
echo "The catalog's daily check opens the update pull request by itself; to go faster (or for"
echo "the first release), open a pull request to darkxex/ps5-homebrew-catalog that sets"
echo "apps/PPSA06464.json to that record (from the dantaipan account)."
