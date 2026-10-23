# Releasing PS64 (Prospero Store) and testing updates

How PS64 gets to the Prospero Store, how updates reach people, and how to test a new
version on the PS5 next to the copy installed from the store.

Status (2026-10-23): **1.0.4 released** (GitHub release `01.000.004`: cartridge option such as
Epyx FastLoad, home-screen music with its own volume, the SID player's C64 Ultimate list;
catalog pull request #634). Listed at https://prosperostore.net/app/PPSA06464/. **Next
release**: collect changes in `docs/release-notes/01.000.005.md` (features and fixes; cosmetic
changes as "Enhancements to the UI"); release when it's ready. Commits use the private GitHub address
`642598+dantaipan@users.noreply.github.com`.

## How the store works

- **Prospero Store** (the PS5 app, `PPSA99000`) installs what the
  [ps5-homebrew-catalog](https://github.com/darkxex/ps5-homebrew-catalog) lists
  (prosperostore.net). PS64's catalog record is `apps/PPSA06464.json`.
- The catalog lists one current release per app: a GitHub release in the PS64 repository,
  with one `.zip` of the `PPSA06464/` app folder (`eboot.bin`, `sce_sys/`, `sce_module/`).
- The store compares each installed app's **`contentVersion`** (from `sce_sys/param.json`)
  with the catalog's, and offers an update when the catalog's is higher.
- An update replaces the app's whole folder. PS64 keeps its own files (settings,
  favourites, recents, indexes, the emulator's games and downloads) in **`/data/ps64`**,
  outside the app folder, so they survive updates. The store keeps the previous version's
  folder in `/data/prosperostore/previous/PPSA06464` until the next update.

## Two copies on one PS5: store copy and dev build

The PS5 allows one install per title ID, so a dev build can't use `PPSA06464` once the store
copy is installed: deploying into `/data/homebrew/PPSA06464` would overwrite the store's copy.

| | Store copy | Dev build |
| --- | --- | --- |
| Title ID | `PPSA06464` | a second ID, e.g. `PPSA06465` |
| Tile name | PS64 | PS64 Dev |
| Installed by | Prospero Store (Install / Update) | `ps5/native/build.sh dev deploy <ps5-ip>` |
| Built in | `ps5/native/out/PPSA06464/` | `ps5/native/out/PPSA06465/` |
| Settings and data | `/data/ps64` | `/data/ps64-dev` (first start copies settings, lists and indexes from `/data/ps64`, else `/data/homebrew/PS64`) |

Only one app runs at a time on the PS5, so the two never compete for the C64's streams.
The dev ID is never listed in the catalog; reserving it too (up to five reservations per
account) keeps anyone else from using it.

A brand-new install (the first `dev deploy`) gets its tile when ShadowMountPlus next
registers folders. On the user's PS5 (fw 13.42) that reliably happened only after a restart and
the jailbreak (its start-up sync); new folders were not always picked up while it was running.
Later deploys of the same title just replace files, and the tile stays.

The dev build has its own games folder too (`/data/ps64-dev/games`); to share the store
copy's, add `games-dir = /data/ps64/games` to `/data/ps64-dev/ps64.cfg`.
`ps5/native/build.sh deploy <ps5-ip>` (without `dev`) installs the store app into
`/data/homebrew/PPSA06464`: only for testing before PS64 is installed from the store.

## Release cycle

1. **Develop and test**: Mac (`make`, `make test`), then the dev build on the PS5.
2. **Raise the version**, both places, kept in step:
   - `contentVersion` in `ps5/native/sce_sys/param.json`, e.g. `01.000.001`
     (format `NN.NNN.NNN`; consoles only see an update when it goes up);
   - `PS64_VERSION` in `src/version.h`, e.g. `1.0.1` (shown in About).
3. **Commit, tag and release**: `ps5/native/release.sh 01.000.001 notes.md` does steps 2-3
   (try it first with `--dry-run`: tests and builds, publishes nothing, restores the version
   files). It checks the repo is clean, on `main` and public, the version is higher and not
   tagged yet; runs `make test`; builds; shows a summary and asks before it commits, tags,
   pushes and publishes; then prints the SHA-256 and writes the catalog record to
   `ps5/native/out/PPSA06464.json`. By hand, the steps are:
   - commit, then tag the commit with the version (`01.000.001`; never a moving tag such
     as `latest`);
   - `ps5/native/build.sh`, which gives `ps5/native/out/PPSA06464.zip`;
   - create a GitHub release for the tag with that zip attached, named `PPSA06464.zip`
     every time; nothing else installable in the release (the catalog rejects releases
     with several installable files, so the websrv `PS64.zip` doesn't go in it);
   - write the release description for players, with what changed first: it becomes the
     release notes on prosperostore.net and in the store (plain text with short headings
     and lists; images, links and HTML aren't carried over).
   - Never replace the asset of a release that's already listed (the catalog pins its
     SHA-256); publish a new version instead.
4. **The catalog follows**: its daily job finds the new release and opens a pull request
   that moves PS64's listing to it. Or open that pull request yourself (from `dantaipan`):
   edit `apps/PPSA06464.json`'s `version`, `artifact_url`, `sha256` (and `icon_url` if the
   icon changed). `sha256`: `shasum -a 256 ps5/native/out/PPSA06464.zip`, or the digest
   GitHub shows next to the asset.
5. **Update on the PS5**: Prospero Store lists PS64 under Updates; one press installs it.
   Check the new version in Options > About.

## First release (one time)

- Make the GitHub repository **public** (the catalog requires it).
- Deal with the old **"alpha 0.1"** pre-release (websrv `PS64.zip`, tag `alpha`): delete it,
  or keep it as a clearly websrv-only release; it shouldn't look like a store release.
- Optional: turn on GitHub's **immutable releases** for the repository (recommended by the
  catalog), so a listed asset can't be replaced by accident.
- Release `01.000.000` as above: `ps5/native/release.sh 01.000.000 notes.md` (the first
  release may use the current version).
- Open the catalog pull request that fills in the reservation `apps/PPSA06464.json`
  (it currently has nulls) with the record `release.sh` wrote to
  `ps5/native/out/PPSA06464.json`: `version` `01.000.000`, `source_repo`
  `https://github.com/dantaipan/PS64`, `artifact_url`
  `https://github.com/dantaipan/PS64/releases/download/01.000.000/PPSA06464.zip`, `sha256`,
  `icon_url` pinned to the tag
  (`https://raw.githubusercontent.com/dantaipan/PS64/01.000.000/ps5/native/sce_sys/icon0.png`),
  `license` `GPL-3.0`. The catalog's checks run on the pull request; a maintainer merges it.
- Install PS64 from Prospero Store; from then on, test with the dev build.

## Tile artwork when testing

The PS5 keeps a title's name, icon and backgrounds from when it was registered. After
changing them in a dev build: delete the tile (Options > Delete), restart the PS5 and run the
jailbreak; ShadowMountPlus registers the folder again on its start-up sync. Store installs and
updates register the title fresh, so people get the current artwork.

## References

- Catalog: [submitting](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/submitting.md),
  [versioning](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/versioning.md),
  [artifact formats](https://github.com/darkxex/ps5-homebrew-catalog/blob/main/docs/artifact-formats.md).
- Store: [ProsperoStore README](https://github.com/blackbearreloaded/ProsperoStore).
- Native build: README section "Native PS5 app (PPSA06464)"; findings in
  `docs/SESSION-NOTES.md`.
