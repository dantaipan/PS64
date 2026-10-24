# Building and developing PS64

How to build PS64 for the Mac and the PS5, run it from the command line, test it without
hardware, and find your way around the source. Using PS64 is in the [README](../README.md);
releasing to the Prospero Store is in [RELEASING.md](RELEASING.md).

The video and audio decoders (`src/vicstream.c`, `src/audiostream.c`) have no OS or SDL
dependencies; the same `src/` code builds for macOS (SDL2) and for the PS5.

## Mac

```sh
make          # builds ps64 and fake_sender (uses third_party/SDL2.framework)
make test     # unit tests (and test_emulator when VICE is built)
```

Fetch the third-party parts once (they aren't kept in the repository):

| Script | What it gets |
| --- | --- |
| `tools/get_sdl2.sh` | The official SDL 2.30.9 macOS framework, into `third_party/` (required) |
| `tools/get_vice.sh` | The VICE libretro core (pinned commit), built into `third_party/vice`; the Makefile includes the emulator whenever that library is there. It also patches VICE's autostart so it works with Epyx FastLoad in |
| `tools/get_sidplayfp.sh` | libsidplayfp + libresidfp for the SID player (Mac or PS5 build) |
| `tools/get_songlengths.sh` | HVSC's `Songlengths.md5` (pinned by SHA-256) |

### Mac app (DMG)

`tools/make_dmg.sh` packages the Mac build as **PS64.app** in `dist/PS64-<version>.dmg`. It
needs all four scripts above; the app carries SDL2 and the song lengths. Started as an app,
PS64 keeps `ps64.cfg` and its `games`, `sids` and `carts` folders in `~/Documents/PS64`.
Apple Silicon only (VICE and libsidplayfp are built for arm64); signed ad hoc, not
notarized.

### Running from the command line

On the C64 Ultimate, either start the VIC stream (port 11000) and audio stream (port 11001)
from its menu (Data Streams, destination = this Mac's IP), or let PS64 start both over the
REST API:

```sh
./ps64 --start 192.168.1.64                 # C64's IP; stops the stream on exit
./ps64 --start 192.168.1.64 --password PW   # if the Ultimate has a network password
./ps64                                      # just listen on UDP 11000 / 11001
./ps64 --multicast 239.0.1.64               # receive a multicast video stream
./ps64 --no-audio                           # video only
./ps64 --audio-latency 100                  # bigger audio buffer for flaky Wi-Fi
./ps64 --config ps64.cfg                    # options from a file (key = value lines)
```

Keys: `F` fullscreen, `M` mute, `S` save BMP snapshot, `N` swap nibble order, `Esc`/`Q`
quit, `O` menu, `B` file browser, `Tab` on-screen keyboard. The window title shows fps and
packet-loss counters, plus audio underruns and dropped packets. Only one viewer can listen
on a port at a time. Audio is 16-bit stereo at ~47983 Hz (PAL), 192 frames per packet,
buffered ~60 ms. If the picture looks like vertical stripes, press `N` (`--swap-nibbles`).

Every config key is also a command-line option (`--key value`); `./ps64 --help` lists them.

### Debug options

- `--type 'print 2+2\n{f1}{run_stop}'` types text and `{key}` names at startup.
- `--screenshot FILE.bmp` saves the window (overlays included) after `--screenshot-ms`.
- `--keys "right,ok,square"` presses UI buttons, one every 1.5 s from 2 s.
- `--osk`, `--osk-mods N`, `--ui menu|home|sid|about|favourites|recents`, `--browse PATH`,
  `--search TEXT`, `--pick`, `--a64-search TEXT`, `--a64-latest CATEGORY`,
  `--launch-favourite N` open screens or start things at launch.
- `--debug-connect IP` acts as if that C64 was picked; `--remote IP` controls a C64 without
  touching its streams (use it for tests, so a PS5's streams aren't redirected).
- `--build-index` builds the search index, prints counts and timing, and exits.
- Emulator: `--c64 emulator`, `--games-dir DIR`, `--emu-load /path/in/games`,
  `--carts-dir DIR`, `--emu-cartridge FILE.crt|off`.
- SID player: `--sid FILE`, `--sid-song N`, `--sids-dir DIR`, `--songlengths FILE`,
  `--home-music 0..10`.

### Testing without hardware

```sh
./fake_sender                    # PAL test pattern to 127.0.0.1:11000
./fake_sender --ntsc --drop 50   # NTSC, dropping ~2% of packets
./ps64
```

`tests/test_input.c` runs small fake FTP (port 21) and REST (port 80) servers where it
needs them; macOS lets them bind without root.

## Forks: your own Assembly64 client ID

Assembly64 identifies apps, not users, so PS64 sends a client ID built into the app:
`PS64`, the official app's. Building PS64 for yourself is fine as it is. If you distribute
your own build under another name, give it its own ID (ask the Assembly64 team for one):

```sh
make A64_CLIENT=MyFork                          # Mac
A64_CLIENT=MyFork ps5/native/build.sh           # PS5 native app (ps5/build.sh likewise)
```

The ID is letters, digits and `_` (it becomes `PS64_A64_CLIENT`; see `src/a64.h`). It isn't
a secret (requests go over plain HTTP), and it's never a per-user setting.

## PS5

### Native app (PPSA06464): the store build

PS64 builds as a native PS5 app, title ID `PPSA06464`, with its own home-screen tile through
[ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus). This is what the Prospero
Store installs.

```sh
ps5/native/build.sh                         # -> ps5/native/out/PPSA06464/ and PPSA06464.zip
ps5/native/build.sh deploy <ps5-ip>         # build and copy to /data/homebrew/PPSA06464
ps5/native/build.sh dev deploy <ps5-ip>     # "PS64 Dev" (PPSA06465), next to the store copy
ps5/native/release.sh 01.000.001 notes.md   # store release (--dry-run to try it)
```

Install by hand: copy the `PPSA06464` folder (from the zip) to `/data/homebrew/` with
ShadowMountPlus running; the tile appears within seconds.

How it's built: the [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
(pinned in `build.sh`, with `boilerplate.patch`) links PS64, VICE, libsidplayfp and PS64's
SDL2 (pinned ps5-payload-dev/SDL, with `sdl-ps5-native.patch`) into a signed development
`eboot.bin`. A native app runs in a stricter environment than a payload, so
`ps5/native/src/` adds: its own memory allocator (the C library's heap is only about 5 MB;
dlmalloc in a 256 MB block, 32-byte aligned as the PS5's C++ ABI expects), non-blocking
sockets via the console's `SO_NBIO` option, C library stand-ins for what the C++ runtime and
VICE expect, and stand-ins for SDL's USB-keyboard and text-dialog calls.
`ps5/native/missing-symbols.sh` lists what the linked libraries need that the console
doesn't provide. Folder listing uses `getdents` (`opendir` is refused in the app sandbox).

The tile artwork is generated: `tools/gen_icon.py` draws the icon and
`tools/gen_backgrounds.py` the two 4K backgrounds (`pic0.dds`, `pic1.dds`). The PS5 keeps a
title's artwork and name from when it was first registered; replacing the app's files
doesn't refresh them.

The native app keeps its files in `/data/ps64/` (`/data/ps64-dev/` for PS64 Dev), because a
store update replaces the app's own folder, and writes a log, `ps64.log`, there. Diagnosing
crashes: see "Diagnosing native crashes" in [SESSION-NOTES.md](SESSION-NOTES.md).

### Payload build (websrv), the older way to run it

The same code also builds as a PS5 homebrew payload (ps5-payload-sdk + the ps5-payload-dev
SDL2 port), run from `/data/homebrew/PS64/` through the websrv launcher. Needs elfldr
(port 9021) and ftpsrv (port 2121), e.g. via the WebKit autoloader.

```sh
ps5/build.sh                 # builds in Docker -> ps5/out/PS64/
ps5/deploy.sh <ps5-ip>       # FTP upload + load websrv
ps5/build.sh dist            # ps5/out/PS64.zip to share (with INSTALL.txt)
```

Then open `http://<ps5-ip>:8080/` and launch **PS64**. Its settings are
`/data/homebrew/PS64/ps64.cfg` (see `ps5/ps64.cfg`); the native app copies them into
`/data/ps64` on its first start.

## Source layout

| File | Purpose |
| --- | --- |
| `src/vicstream.[ch]`, `src/audiostream.[ch]` | Portable video and audio packet decoders |
| `src/main_sdl.c` | The app: options, sockets, SDL2 display and audio, the main loop |
| `src/c64api.[ch]` | Minimal HTTP client for the Ultimate REST API |
| `src/c64keys.[ch]`, `src/c64remote.[ch]` | C64 key table; background thread sending keys/joystick |
| `src/controls.[ch]` | Controller and keyboard mapping |
| `src/osk.[ch]`, `src/osk_sdl.[ch]` | On-screen keyboard |
| `src/font8x8.h`, `src/font_sdl.[ch]` | Overlay font (`tools/gen_font.py`) and drawing |
| `src/ui.[ch]`, `src/ui_sdl.[ch]` | Menus, settings, browser, search, Assembly64, SID player screens |
| `src/home.[ch]`, `src/home_sdl.[ch]`, `src/mesh3d.[ch]`, `src/models.[ch]` | The 3D home screen |
| `src/prompts.[ch]`, `src/prompts_sdl.[ch]`, `src/prompts_svg.h` | Button icons in hints (`tools/gen_prompts.sh`) |
| `src/discover.[ch]` | Finds Ultimates on the local network |
| `src/ftp.[ch]`, `src/c64files.[ch]` | The C64's storage over FTP; starting files, cartridges over REST |
| `src/settings.[ch]` | Saves choices back into the config file |
| `src/c64index.[ch]` | Search index: crawl, save/load, keyword search |
| `src/a64.[ch]`, `src/shots.[ch]`, `src/image.[ch]` | Assembly64 client; CSDb screenshots |
| `src/favs.[ch]` | Favourites and recents |
| `src/emulator.[ch]`, `src/emugames.[ch]` | Built-in emulator (libretro frontend for VICE); starting games in it |
| `src/localfs.[ch]` | Local folders (games, SIDs, carts, USB drives) |
| `src/sidplay.[ch]`, `src/playlist.[ch]`, `src/sidrandom.[ch]` | SID player engine wrapper, play order, the home music's random pick |
| `src/version.h` | PS64's version (About; matches the native app's contentVersion) |
| `tests/` | Unit tests (`test_input.c`, `test_vicstream.c`) and `test_emulator.c` against the real core |
| `tools/fake_sender.c` | Synthetic stream generator |
| `ps5/` | Payload build (`Dockerfile`, `Makefile`, `ps64.cfg`, `INSTALL.txt`) |
| `ps5/native/` | Native app build: scripts, `sce_sys/param.json`, patches, native-only sources |
