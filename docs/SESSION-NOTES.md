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

- Try Assembly64 search end to end on the PS5 now that the client ID is configured.
- Try emulator mode on the PS5 (speed, sound, controller, Assembly64 downloads).
- Possible later: SID playback in the emulator (a PSID player stub), save states.
- 1.0.0 released and listed in the Prospero Store 2026-10-19 (catalog PR #435 merged).
- Unreleased, for 1.0.1 (draft notes `docs/release-notes/01.000.001.md`): Mount & run loads
  from drive A's actual device number. Found while debugging the user's setup: an
  unpowered real 1541-II on the serial bus (device 9) made all loads fail with FILE NOT FOUND;
  powering it on (before the C64U) or unplugging it fixed that, no code needed.
- Store release and the update process: `docs/RELEASING.md`; dev build
  (`ps5/native/build.sh dev`, PPSA06465 "PS64 Dev", /data/ps64-dev) and
  `ps5/native/release.sh` (with `--dry-run`) are ready. Publishing only with the user's go-ahead.
- Optional: faster disk loading (Ultimate drive/speed settings), not investigated.
