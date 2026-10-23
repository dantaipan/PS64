# PS64 development notes (archive of the first build sessions, Oct 2026)

A record of how PS64 came to be, the decisions made along the way and why,
what was learned about the hardware, and what is still open. Written so that
a later session (or person) can pick the project up without the original
conversation.

## Setup

| Thing | Value |
| --- | --- |
| C64 | Commodore 64 Ultimate (commodore.net), firmware **1.1.0**, core 1.49, at **192.168.2.64** (hostname C64-Ultimate-5F3538) |
| C64 services | Web Remote Control (REST, port 80) **on**, FTP (port 21) **on**, no network password |
| PS5 | Firmware **13.42**, jailbroken with the WebKit autoloader v0.6.1, at **192.168.2.195** |
| PS5 payloads | pldmgr payload manager autoloads kstuff-lite, ShadowMountPlus, nanodns, ftpsrv (port **2121**), pkg-manager; **websrv** (port 8080) started from the payload manager to launch homebrew |
| Mac | Apple Silicon, no Homebrew; Docker Desktop for the PS5 SDK; GitHub CLI in `~/.local/bin/gh` |
| Repo | https://github.com/dantaipan/PS64 (private, GPL-3.0) |
| Assembly64 | client ID **PS64** (registered by the Assembly64 admin) |

## What PS64 is

A viewer and controller for the C64 Ultimate, for macOS (SDL2) and as PS5
homebrew. It shows the Ultimate's video (UDP 11000) and audio (UDP 11001)
data streams and drives the C64 from a game controller.

Features, in the order they were built:
1. Video stream decoder (`src/vicstream.c`): 384x272 PAL, 4 bpp, low nibble first.
2. Audio stream decoder (`src/audiostream.c`): 16-bit stereo ~47,983 Hz, 192 frames/packet;
   ~60 ms buffer with start-up flush, priming and drain-to-target.
3. PS5 build: ps5-payload-sdk + a static SDL2 from the ps5-payload-dev SDL port, built in
   Docker (`ps5/Dockerfile`, image `ps64-ps5-sdk`). Runs as `/data/homebrew/PS64/eboot.elf`,
   launched from websrv (no PKG: fake PKGs don't install on 13.42).
4. Controller input to the C64 (`src/c64remote.c`, `src/c64keys.c`, `src/controls.c`).
5. On-screen C64 keyboard (`src/osk.c`) with what-you-type labels: Shift/C=/CTRL relabel keys,
   PETSCII graphics glyphs (`tools/gen_font.py`), colour names.
6. Network discovery and C64 picker (`src/discover.c`), saved to the config (`src/settings.c`).
7. Options menu (`src/ui.c`, `src/ui_sdl.c`).
8. File browser over FTP (`src/ftp.c`, `src/c64files.c`): Run / Load / Mount & run / Run cartridge / Play.
9. Search over the C64's storage with an FTP-built index (`src/c64index.c`).
10. Reset / Reboot in the menu; "Mount & run" reboots first (fixes crashes when switching games).
11. Assembly64 online search (`src/a64.c`) with downloads sent straight to the C64.
12. Favourites (Games / SIDs) and Recents (last 25) (`src/favs.c`).
13. Renamed from "C64 View" to **PS64** (folder, files, icon via `tools/gen_icon.py`).
14. **Built-in emulator** (VICE) as an alternative machine (`src/emulator.c`, `src/emugames.c`,
    `src/localfs.c`): games folder, Assembly64 downloads, multi-disk swapping, fast loading,
    real joystick and keys, local search index. See "Emulator mode" below.

## Controls (current)

| Controller | Action |
| --- | --- |
| D-pad / left stick | Cursor keys (joystick only on firmware with the REST input command) |
| Cross | RETURN (fire on firmware with the input command) |
| Square / Triangle / Circle | SPACE / RETURN / RUN/STOP (configurable `button-*`) |
| L1 / R1 / L2 / R2 | F1 / F3 / F5 / F7 |
| Touchpad | On-screen keyboard |
| L3 | File browser (L2 there: search; Triangle in lists: favourite) |
| R3 | Ultimate menu button |
| Options | Menu: browse, search, Assembly64, favourites, recents, reset, reboot, audio, port, choose C64, update index, quit |

## Key findings and decisions

- **Keyboard input on firmware 1.1.0** goes into the KERNAL keyboard buffer (`$0277`, count `$C6`)
  via REST `readmem`/`writemem`, injecting only when the buffer is empty. It works in BASIC,
  loaders and menus, **not in games** (they scan the CIA keyboard matrix directly). RUN/STOP
  can't break a running program; RESTORE isn't possible. PETSCII codes come from the C64's own
  KERNAL tables (`$EB81/$EBC2/$EC03/$EC78`), verified against the device.
- **`POST /v1/machine:input`** (Ultimate firmware 3.15+) gives real keys and joystick; it returns
  404 on C64 Ultimate 1.1.0. PS64 probes it at start and switches automatically when available.
- **Joystick hack via CIA `$DC00` writes was built and then removed at the user's request**
  (keyboard control only; don't reintroduce it).
- **Stream names are `video` and `audio`** (not `vic`); `ip=<addr>:<port>` is accepted.
- The PS5 SDL port has no GPU renderer: use a renderer without `SDL_RENDERER_ACCELERATED`.
- The PS5 SDL port doesn't map the Create button; controller events come as SDL game-controller
  events (don't also handle raw joystick events, they duplicate).
- Discovery: probing `/v1/info` over the /24 must wait on real time (refusals make `poll()` return early).
- The C64's FTP server once stopped answering until the C64 was restarted; a full index of `/SD`
  (66,090 files, 5,119 folders, ~18 min, 60,821 of them HVSC SIDs) later ran without failures.
- Search ranking treats "folder/file" as the title (collections use `Title/Disk1.d64`), prefers
  runnable files over SID/MOD, whole words over partial, names starting with the words.
- Assembly64: only a `client-id` header (spec: `https://hackerswithstyle.se/leet/v3/api-docs`);
  plain HTTP works. The live category list has names but no ids; result category numbers are
  learned from filtered searches (music = 4).
- Folder favourites launch the first runnable file: disk images first in name order (Disk1),
  then PRG, CRT, SID. Verified on the real C64 with Boulder Dash (about 60 s load at 1541 speed).

## Emulator (VICE) feasibility spike: passed

Goal: let people without a C64 Ultimate use PS64 with a built-in emulator. Spike code lives in
`spikes/vice/` (excluded from Git; it contains VICE clones and a game disk image for testing).

- Core: `mihawk-99/PS5_VICE` (fork of `libretro/vice-libretro`, VICE 3.10), built as a static
  library with the PS64 SDK (`STATIC_LINKING=1`, `prospero-clang`). The frontend must supply 12
  libretro-common helper files and link `-lc++ -lc++abi -lunwind` (maths is in libc; no `-lm`).
- C64 ROMs are embedded in the core; no ROM files needed (the CMD FD-2000/4000/HD ROM warnings are harmless).
- Mac: keyboard and joystick proven (joystick register 31 -> 14 with up+fire); Boulder Dash
  autostarts; ~11-13x real time.
- PS5: ran headless from websrv, loaded Boulder Dash, **4.8x real time (240 fps)**.
- Licensing: VICE is GPL-2.0-or-later, compatible with PS64's GPL-3.0.
- Next step if pursued: a libretro frontend inside PS64 plus an "emulator instead of C64
  Ultimate" choice, local file browsing, Assembly64 -> emulator, disk swapping.

## Emulator mode (built 2026-10-18)

- Core: same pinned fork as the spike, fetched/built by `tools/get_vice.sh` into
  `third_party/vice` (Mac objects in `build-osx`, PS5 in `build-ps5`; `ps5/build.sh` fetches
  on the host and builds inside Docker). Mac Makefile includes it when the library exists.
- `src/emulator.c` is the libretro frontend. Things learned:
  - The core keeps global state: set it up once per process. Starting it again = power cycle.
  - It must run one frame after `retro_load_game(NULL)` before another load, or it sets
    itself up twice and crashes (found when `--emu-load` loaded straight away).
  - Loading into a running core is only a plain reset; call VICE's `reload_restart()` after
    `retro_load_game` to autostart the new file.
  - Keys go straight into VICE's keyboard matrix with `keyboard_set_keyarr(code>>3, code&7)`
    (PS64 key ids are matrix codes); RESTORE is `machine_set_restore_key`. Taps are timed in
    emulated frames. Joystick via the RetroPad (B = fire), port via the `vice_joyport` option.
  - Fast loading: option `vice_autoloadwarp`; the core asks for fast-forward through
    `SET_FASTFORWARDING_OVERRIDE`, and PS64 then runs frames for 12 ms per loop, muted.
  - Pacing: 50.125 frames/s on the host clock, nudged ±1% by the sound queue level; the
    audio device is reopened at 48 kHz in emulator mode (Ultimate: ~47983 Hz).
  - Name clash: VICE has its own `ui_init` (PS64's became `ui_setup`) and `emu_reset`
    (PS64's emulator code uses `emulator_*`).
- Multi-disk: a disk is started with the other disk images of its folder or Assembly64 entry
  via an `.m3u` in `emulator/`; the menu's Disk item swaps through the disk-control interface.
- Games folder paths look like the Ultimate's (`/Folder/Game.d64`, `/` = games folder), so
  browser, search index (`host=emulator`, `ps64-emu-index.txt`) and favourites
  (`FAV_EMU_FILE/FOLDER`) are shared code. SID/MOD can't play in the emulator.
- Tested: `tests/test_emulator.c` (real core: typing, joystick 31->14, PRG, .m3u + swap,
  folder launch, Assembly64 download path, power cycle) and the Mac app (Boulder Dash from the
  games folder and from Assembly64). PS5 build: eboot.elf 6.8 MB.

## Native app (Prospero Store) spike, 2026-10-19: passed

Goal: list PS64 on prosperostore.net (catalog: github.com/darkxex/ps5-homebrew-catalog), which
only takes native apps (`PPSAxxxxx/eboot.bin` + `sce_sys/param.json`, mounted by ShadowMountPlus).
**Title ID `PPSA06464` is reserved for PS64** (catalog PR #427, merged). Listing also needs a
public repo, a release tagged with the version (e.g. `01.000.000`) carrying `PPSA06464.zip`,
`contentVersion` raised each release, and a PR adding the full record.

Spike in `spikes/native/` (gitignored): blackbearreloaded/ps5-native-app-boilerplate built in a
Docker image (`ps64-native` = `ps64-ps5-sdk` + ninja, ccache, clang-format/tidy, pkg-config),
`make init TITLE_ID=PPSA06464`, then a C test app with SDL2; `make deploy PS5_HOST=...`.
On the PS5 everything PS64 needs works: SDL picture, controller, sound, `/data` and `/download0`
writes (ShadowMountPlus mounts `/data` into the sandbox; no elevation), C64 REST + FTP,
Assembly64 DNS + HTTP, the C64's UDP video stream at full rate. What it took:

1. **Heap**: the C library heap in a native app is ~5 MB. `malloc` & co. go to dlmalloc
   (gee.cs.oswego.edu, 2.8.6) in one mmap'd block (`--wrap` malloc free calloc realloc
   posix_memalign memalign aligned_alloc; free/realloc pass foreign pointers to the real ones).
   1 GB/768/512 MB mmaps failed, 384 MB succeeded (the flexible-memory budget is ~450 MB);
   direct memory (separate) also works.
2. **Sockets**: `fcntl(O_NONBLOCK)` doesn't take effect on sockets; a non-blocking recv blocked
   forever (looked like a black screen). Wrap `fcntl`: on sockets set `SO_NBIO` (0x1200).
3. **Host names**: `getaddrinfo` isn't usable (crash at address 0); use `sceNetResolver` first.
4. **SDL**: PS64's SDL (ps5-payload-dev/SDL `ee4c47d`), rebuilt with patches: flush the frame
   from the CPU cache before each flip, 100 ms timeout on the flip-event wait. SDL's USB
   keyboard and IME dialog calls are wrapped to "not available" (their modules aren't loaded
   in a native app: crash at address 0). PacBrew's SDL2 also links, with `-L` to the SDK stubs.
5. **libc gaps**: `nl_langinfo`, `___mb_cur_max` stand-ins.
6. **Quitting**: returning from `main` is SIGSYS; call `sceSystemServiceLoadExec("exit", NULL)`.
7. Drew at 30 fps because the test loop also slept 16 ms after the vsync'd present.
- Debugging: ShadowMountPlus's `/data/shadowmount/debug.log` shows crashes (signal, RIP,
  return address; map with llvm-symbolizer on `build/llvm-pie.elf`, base 0x400000). After PS5
  rest mode ShadowMountPlus stopped scanning until restarted (new tiles didn't appear).

## Native app build (2026-10-19): runs on the PS5

`ps5/native/build.sh [deploy <ps5-ip>]` builds PS64 as `PPSA06464` (see README "Native PS5
app"). Status: tested by the user on the PS5 (fw 13.42): C64 streaming, controller and typing,
on-screen keyboard, menu, file browser + search, Assembly64, and the emulator (Boulder Dash
from Assembly64 with sound, keyboard and joystick) all work. Imports only system modules the
spike proved. Beyond the spike, it needed:
- the SDK's static libc++/libc++abi/libunwind for VICE's reSID-fp (iostreams, exceptions),
  so `libc_native.c` adds "C"-locale stand-ins (`*_l` functions, newlocale, rune tables,
  catopen...), `__emutls_get_address` (pthread keys), `__assert`, `__dso_handle`, and
  `arc4random`/`isatty`/`strcasestr` (otherwise only in libScePosixForWebKit);
- boilerplate.patch: `-L` to the SDK libs (libc++ asks for libpthread by name) and
  `__eh_frame_*` symbols in the linker script for the C++ unwinder;
- archive names limited to `[A-Za-z0-9_.-]` (libc++.a staged as libcxx.a);
- `getcwd` crashes in a native app (VICE calls it while booting): wrapped to return `/app0`;
- Home-screen artwork: `tools/gen_backgrounds.py` writes BC7 DDS backgrounds directly
  (solid 4x4 blocks, mode 6), so no Windows texconv is needed. The PS5 caches a title's
  artwork/name/music at first registration in `/user/appmeta/PPSA06464/` and
  `/user/app/PPSA06464/sce_sys/`; after the spike's template art (bears, music, "PS64 Spike")
  those were updated over FTP by hand.
- Menu: "Choose C64 stream or emulator", "About PS64" (text in `ui_about_text()`, thanks to
  VICE and Assembly64), version in `src/version.h`.
- `access()` reported an existing file as missing, so the first-run copy overwrote the copied
  config: existence is checked with fopen. Like getaddrinfo, some libc calls that resolve
  fine still fail in the sandbox; ShadowMountPlus logs crash details only in an app's first
  60 s, so reproduce early (e.g. `c64 = emulator` to start straight into the emulator).
Native-only behaviour in `main_sdl.c` (`PS64_NATIVE`): own `main` (no SDL_main; fake argv),
quit via `sceSystemServiceLoadExec("exit")`, files in `/data/ps64` (first run copies cfg,
favourites, recents, indexes from `/data/homebrew/PS64`), log `/data/ps64/ps64.log`, no
PS5 notifications, no 4 ms loop delay (present already waits for the display).
Test plan on the PS5: tile appears; streaming + controller + typing; menu; file browser and
search over FTP; Assembly64 search + download; emulator (Boulder Dash) with sound and
joystick; favourites/recents carried over; Options > Quit returns cleanly.

## Store release and after (2026-10-19 / 20)

- **1.0.0 is in the Prospero Store** (https://prosperostore.net/app/PPSA06464/; GitHub release
  `01.000.000`; catalog PR darkxex/ps5-homebrew-catalog#435 merged by a maintainer). The repo
  is public. The old websrv "alpha" release was deleted.
- **Privacy:** all commits use `642598+dantaipan@users.noreply.github.com` (history rewritten
  twice: before going public, and after a README edit made on github.com used the Gmail
  address). The user set this as their global git email; the earlier catalog reservation
  commit (#427) still carries the Gmail address and can't be changed.
- **Tile art** (`tools/gen_icon.py`, `tools/gen_backgrounds.py`): rainbow bands red->blue,
  2 px gaps, inset from the left, "PS64" aligned with the bands; backgrounds are generated
  BC7 DDS. After changing art of an installed tile: delete the tile, restart the PS5, jailbreak
  (ShadowMountPlus re-registers on its start-up sync; new tiles also needed a restart).
- **Dev build** "PS64 Dev" (`PPSA06465`, `/data/ps64-dev`) is installed on the PS5 next to
  PS64; test changes there: `ps5/native/build.sh dev deploy 192.168.2.195`.
- **Release** with `ps5/native/release.sh VERSION notes.md [--dry-run|--yes]`
  (see `docs/RELEASING.md`). The user wants fixes **bundled into 1.0.1**, released only when
  they say so. Draft notes: `docs/release-notes/01.000.001.md`.

### Released in 1.0.1 (2026-10-20)
- Mount & run reads drive A's bus ID from `GET /v1/drives` (`files_parse_drive_a`) and types
  `LOAD"*",<id>,1`; says so if drive A is off. (The user's FILE NOT FOUND turned out to be an
  unpowered real 1541-II on device 9 hanging the serial bus: power it on or unplug it.)
- Search Assembly64 **R1 = latest 25 releases** (category from L2, games if "All"). The API has
  no such call: query `(latest:1month) & (category:KEY) & (sort:year) & (order:desc)`
  (`latest` = added to Assembly64 recently, includes old games newly archived), fetch 200,
  sort by `released` date client-side, widen to 3months / 1year if fewer than 25 dated items.
  Debug: `--a64-latest CATEGORY`.
- Assembly64 names decoded from HTML entities (`&lt;` etc.).
- Search screens show 20 result rows when the results have focus (10 while typing).
- **CSDb screenshots** beside Assembly64 results (`src/shots.[ch]` worker + cache of 16,
  `src/image.[ch]` with vendored `src/stb_image.h`). Assembly64 metadata `images` fields are
  always empty; for CSDb-sourced items the id is the CSDb release id and the picture is
  `http://csdb.dk/gfx/releases/<id/1000*1000>/<id>.gif` (384x272 GIF). CSDb categories come
  from the presets' `subcat` list (names "CSDB ...", ids 0..10; others: C64.org 11, C64.com
  14/15, Gamebase64 16, HVSC 18-20, Guybrush 26-31, OneLoad64 33). `main_sdl.c` asks for the
  highlighted item when the selection changes and hands the pixels to `ui` (`a64_shot*`);
  `ui_sdl.c` `draw_shot` keeps one texture keyed on the pixel pointer.
  The "buggy" picture changes were the texture keyed on the pixel pointer (same-size
  pictures reuse the freed address): now keyed on a serial. Several pictures per release are
  one animated GIF; all frames (up to 8) are decoded and cycled.

## 1.0.2 (2026-10-21): home screen, audio level

- **Home screen** (`src/home*.c`, `src/mesh3d.[ch]`, `src/models.[ch]`): 1920x1080 canvas,
  three objects built in code (the linked Sketchfab/GrabCAD models can't be redistributed):
  1702 (Vice: BASIC start-up screen), 1084 (Stream: live picture once found, TV static
  otherwise), SID chip. Drawn with `SDL_RenderGeometry` (the PS5 has only SDL's software
  renderer: flat shading, back-face culling, painter's sort with per-part layers; the chip's
  pin rows are separate meshes drawn far row/body/near row). Holds 60 fps on the PS5.
  - Power/settings icons: Material Icons (Apache 2.0) rasterised with nanosvg (zlib).
  - Home replaced the C64 picker: settings (the gear) is the old picker; Options > Home is the
    first menu item; the emulator pauses at Home and resumes with Vice. On Home, PS64 finds
    the saved C64 (or the only one) with `discover` and starts its stream; audio is silent.
- **Audio level**: Options > Audio steps 0..-30 dB (2 dB), then Off; separate levels per
  source (`stream-volume`, `emulator-volume`, later `sid-volume`), all default -12 dB.
- Release notes style (user): features and real fixes only; cosmetic changes are one line,
  "Enhancements to the UI".

## 1.0.3 (2026-10-22): SID player, USB, fixes

- **SID player** (UI_SID; `src/sidplay.[ch]`, `src/playlist.[ch]`): libsidplayfp 3.1.1 +
  libresidfp 1.2.3 (GPL-2+, `tools/get_sidplayfp.sh`, pinned tarballs; cross-built for the
  PS5 with its configure in the container). VICE has its own older reSIDfp in the same
  namespace, so this copy is built with `-DreSIDfp=ps64_reSIDfp`. About 9.5% CPU on the PS5.
  - Lists: Local (SID folder `/data/ps64/sids`), Assembly64 (search with `type:sid`, R1
    latest; downloads cached in `sid-cache`). Play on PS64 or the C64 Ultimate (POST
    `runners:sidplay?songnr=N`, 1-based, verified). Playlist in order/shuffle/repeat-one,
    subtunes, visualiser (voice notes from the SID registers, oscilloscope).
  - Song lengths: HVSC `Songlengths.md5` is HTTPS-only (PS64's HTTP client is plain HTTP), so
    `tools/get_songlengths.sh` (pinned SHA-256) bundles it in `/app0/assets`.
- **Random crashes**: the PS5's C++ ABI expects 32-byte aligned `new`
  (`__STDCPP_DEFAULT_NEW_ALIGNMENT__` 32, AVX2 `vmovaps ymm`); `ps5/native/src/ps64_heap.c`
  dlmalloc now uses `MALLOC_ALIGNMENT` 32 (was 16).
- **Native sandbox**: `opendir()` fails (EPERM); `src/localfs.c` lists with `open()` (no
  O_DIRECTORY) + `getdents()`.
- **USB drives**: `/mnt/usbN` (ShadowMountPlus mounts them into the app's sandbox) appear as
  folders `USB0..USB7` at the top of the games folder and the SID player (only mounted or
  non-empty ones: the PS5 keeps empty mount points); `usb-prefix`; the index covers them.
- **Folders 0777**: PS64 made `games` 0755 (owned by the app), so FTP couldn't add files;
  folders are now created 0777 and opened up at start-up.
- **Assembly64 client ID** is per app (the Assembly64 admin confirmed): built in as "PS64"
  (`A64_CLIENT_ID`), the setting and its entry screen removed (old config lines ignored).
- Catalog PRs from the `dantaipan` fork are recognised as developer release bumps and merged
  automatically (#489 1.0.1, #527 1.0.2, #584 1.0.3).

## Unreleased (on main, for 1.0.4)

- SID player: "Play on" shows the choice (it showed the playing tune's output).
- SID player: its Assembly64 search and the games search don't share results; choosing
  another machine on Home starts a fresh search.
- SID player: a third list, **C64 Ultimate** (while connected): its storage over FTP
  (`ftp_get` / `files_get` -> FR_FILE), plays as "Play on" says. Not yet tried on the PS5.
- Hints use **PlayStation button icons**: Kenney Input Prompts 1.5 (CC0), PlayStation set,
  embedded by `tools/gen_prompts.sh` into `src/prompts_svg.h`; hint strings use tokens
  (`"{cross} play  {l1}{r1} page"`, `src/prompts.[ch]`, `src/prompts_sdl.c`); D-pad icons
  recoloured (directions white, others 60%). Only PS64's own hint strings use tokens.
- The keyboard's own hint line shows only when typing onto the C64 (not on PS64's screens).
- **Cartridge option** (Options > Cartridge, both machines). Not yet tried on the PS5, nor
  on the real C64 Ultimate (no .crt in its `/flash/carts` yet; ask before the first switch).
  - Emulator: `emulator_set_cartridge()` calls VICE's `cartridge_attach_image(0, crt)`.
    The core detaches every cartridge on each load (`retro_unload_game`, and
    `initcmdline_cleanup` in `reload_restart`), so PS64 re-attaches it after loads and power
    cycles (not when the load is itself a CRT). VICE's `CartridgeReset` power-cycles on
    attach. Epyx FastLoad prints "FASTLOAD" under READY., which made VICE's autostart give up
    ("AUTOSTART: Turned off"); `tools/get_vice.sh` patches `check()` in `autostart.c` to also
    accept READY. one line higher (it deletes the built `.a` files so both get rebuilt). Its own `vice_cartridge` core option isn't used: it
    scans with `opendir` (EPERM on the PS5). Files: `carts/` (`carts-dir`), saved as
    `emu-cartridge` (`off`). Tested on the Mac with a homemade 8K .crt that turns the border
    red (BASIC shows 30719 bytes free).
  - C64 Ultimate (firmware 1.1.0, checked read-only, and against the 1541ultimate source):
    `C64 and Cartridge Settings / Cartridge` holds a file name in `/flash/carts` (`list_crts`
    in `c64.cc`); it loads at reset. PS64 lists the folder over FTP, reads `current` with
    GET, sets it with `POST /v1/configs` (JSON, so "" works for Off), then
    `PUT /v1/machine:reboot`. It never calls `configs:save_to_flash`, so the change lasts
    until the C64 is switched off. `POST /v1/configs` exists on 1.1.0 (a text/plain probe
    gave 400 "Content type should be 'application/json'").
- **Home music**: `src/sidrandom.[ch]` walks the SID folder on a thread (skips USBn,
  at most 5000 folders, reservoir sampling). With no tunes it falls back to a random one of
  Assembly64's latest `type:sid`. `sp.home` marks the tune: always on PS64; at its end or on
  Square another; it stops for Vice, Stream and the SID player, and restarts at once back on Home.
  Assembly64 searches (games, Vice and Ultimate) start on the Games category. Its level is
  `home-music` 0..10 (default 5; 0 off; dB = -4 x (10 - level)), set in the home screen's
  settings panel (the gear; not the Options menu: it's app-level). localfs's PS5 getdents buffer is now behind a mutex (the
  walk runs off the main thread).
- Notes so far: `docs/release-notes/01.000.004.md`. Release only when the user says so.

## Diagnosing native crashes

- ShadowMountPlus writes RIP/backtrace to `/data/shadowmount/debug.log`, but only in an app's
  first 60 s: reproduce soon after launch. Addresses = `llvm-pie.elf` + 0x400000; keep
  `third_party/ps5-native-app-boilerplate/build/llvm-pie.elf` of the build being tested and
  look addresses up with `prospero-nm` / `llvm-objdump-18` in the `ps64-native` container.
- `ps64.log` gets a `CRASH:` line from PS64's signal handler when the process gets a signal
  (not when the system kills it), and SID player / audio-reopen milestones.
- Debug keys for the UI on the Mac: `--keys "right,ok,r1,wait,ok"` (one every 1.5 s).

## Working conventions

- Build: `make` / `make test` (Mac; run `tools/get_sdl2.sh` once), `ps5/build.sh all dist`,
  `ps5/deploy.sh 192.168.2.195 --no-websrv` (keeps the PS5's `ps64.cfg`).
- Testing without hardware: `fake_sender`, fake Ultimate / fake Assembly64 Python servers
  (bind to all interfaces on port 80; macOS allows that without root), and debug options
  (`--remote`, `--type`, `--screenshot`, `--osk-mods`, `--ui`, `--pick`, `--browse`,
  `--search`, `--debug-connect`, `--build-index`, `--a64-search`, `--launch-favourite`).
- Use `--remote` (not `--start`) for Mac tests so the PS5's streams aren't redirected.
- The user approves plans before larger features, and asks before anything runs on the real C64
  that changes its state.

## Open items

- **Next:** install PS64 Dev when the PS5 is on (the SID player's C64 Ultimate list and the
  D-pad icon colours are built but not yet on the PS5), then the user tests the C64 Ultimate
  list (browse /SD or /USB0, play on PS64 and on the Ultimate), the Cartridge option (with
  their own Epyx .crt in `/data/ps64/carts`) and the home music. Keep collecting 1.0.4 fixes.
- Possible: a README/About note that an unpowered real drive on the bus blocks loading.
- Possible: faster disk loading (Ultimate drive/speed settings), save states.
- The Prospero Store's own self-update was broken in its 1.000.070 (fixed upstream); the user's
  store is on 1.000.104. Opening the store can leave ShadowMountPlus waiting on a "same-app
  handoff": restart + jailbreak before launching PS64 if it won't start.
