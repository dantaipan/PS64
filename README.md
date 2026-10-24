# PS64

Play the Commodore 64 on a jailbroken PS5 (or a Mac), with or without the real hardware.

- **Stream**: show a **Commodore 64 Ultimate**'s picture and sound and drive it from the
  DualSense: on-screen keyboard, file browser, search, favourites.
- **Vice**: no C64? PS64's **built-in emulator** (VICE) runs one. Copy in games or download
  them from Assembly64.
- **SID**: a **music player** for C64 tunes, on PS64 itself or on the Ultimate's SID chips.

Both machines can search the online **Assembly64** database and start what you find straight
away.

*Vice in PS64 is a deliberately simple C64-only emulator. For something more feature-rich,
use a dedicated emulator.*

## Install

**PS5** (jailbroken, with [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)):
install **PS64** from the [Prospero Store](https://prosperostore.net/app/PPSA06464/), and
update it there. It gets its own tile on the PS5's home screen. PS64 keeps your settings and
files in **`/data/ps64/`**, so updates don't touch them. Copy files there with an FTP client
(the PS5's FTP server, port 2121).

**Mac** (Apple Silicon): build PS64 yourself; see [docs/BUILDING.md](docs/BUILDING.md).
Run as an app, it keeps its settings and folders in **`~/Documents/PS64`** (the Mac paths
below).

**C64 Ultimate** (for Stream): turn on **Web Remote Control** (the REST API) in its network
settings, and **FTP** for the file browser. PS64 finds it on the network by itself.

## The home screen

PS64 starts on a 3D home screen with three objects: **Vice** (the emulator), **Stream** (the
C64 Ultimate, shown live on the monitor once found) and **SID** (the music player). Left/Right
choose, Cross starts. **Options > Home** goes back there from a machine.

- **Settings** (Options, or the gear icon): the C64 Ultimates found (pick one, enter an
  address, or give its network password) and the **Home music** volume.
- **Home music**: a random tune from your SID folder plays on the home screen (one of
  Assembly64's latest if the folder is empty). Square plays another, Triangle turns it off
  or on; volume 1 to 10 in Settings (0 is off). It stops when you open a machine.

## Controls

| Controller | On the C64 |
| --- | --- |
| D-pad / left stick, Cross | Joystick and fire |
| Square / Triangle / Circle | SPACE / RETURN / RUN/STOP |
| L1 / R1 / L2 / R2 | F1 / F3 / F5 / F7 |
| Touchpad click | On-screen keyboard (every key, with Shift / C= / CTRL) |
| L3 | File browser |
| R3 | The Ultimate's own menu |
| Options | PS64's menu |

**On-screen keyboard**: D-pad moves, Cross types, Circle closes, Square DEL, Triangle SPACE,
R2 RETURN, L1 Shift (twice locks), R1 C=, L2 CTRL. Keys show what they'll type with the
current modifiers. The shortcut buttons can be changed (`button-square = f2`, etc.).

**The menu (Options)**: Home, Resume, browse and search files, Search Assembly64, Favourites,
Recents, Disk (multi-disk games), Reset C64, Reboot / Switch off and on, D-pad mode (Vice),
Cartridge, Audio level (Left/Right; Cross mutes), Joystick port, Update search index, About,
Quit. On screens with lists, the hint line at the bottom shows the buttons.

On the Mac, any SDL-supported controller works, plus the keyboard: **O** menu, **B** file
browser, **Tab** on-screen keyboard, **F** fullscreen, **M** mute, **Q** quit.

## Stream: the C64 Ultimate

PS64 starts the Ultimate's video and audio streams, shows them, and stops them when you quit.
It remembers the C64 you chose.

- **File browser** (L3, or Options > Browse files): the Ultimate's storage (`/SD`, `/Flash`,
  `/Temp`, USB). Cross opens a folder or starts a file; Square shows more actions; Circle
  goes up; L1/R1 page.

  | File | Cross | Square |
  | --- | --- | --- |
  | `.prg` | Run | Load |
  | `.d64 .d71 .d81 .g64 .g71` | Mount & run (reboots, mounts on drive A, `LOAD"*",8,1`, `RUN`) | Mount on A or B only |
  | `.crt` | Run cartridge | |
  | `.sid` / `.mod` | Play | |

- **Search files**: finds files by name across the C64's storage. The first time, it builds
  an index of the folders in `search-roots` (default `/SD`, e.g. `/SD,/USB0`); you can search
  while it builds. **Update search index** refreshes it.
- **Reset C64** restarts it (a running cartridge restarts too); **Reboot C64** also clears
  the cartridge.
- **Typing**: on the C64 Ultimate's firmware 1.1.0, keys go into the C64's keyboard buffer.
  That works in BASIC, loaders and most menus, but not in games that read the keyboard
  directly, and there's no joystick. Real key presses and the joystick come with firmware
  that has the Ultimate's input command; PS64 uses it automatically when it's there.

## Vice: the built-in emulator

A C64 inside PS64: keyboard and joystick work in games, multi-disk games swap disks, and
disks load at full speed. **Options > Home** pauses it; **Vice** carries on where it was.

- **Games folder**: `/data/ps64/games` (Mac: `~/Documents/PS64/games`). Copy in D64, G64,
  D71, D81, PRG, T64, TAP and CRT files, in folders if you like. **Browse games folder** and
  **Search games folder** work like the Ultimate's; Square on a disk offers **Insert into
  drive 8** without starting it.
- **USB drives** plugged into the PS5 appear at the top as **USB0**, USB1...
- **Assembly64** downloads are kept (in `emulator/downloads/`) and start from there next time.
- **Multi-disk games**: starting a disk also loads the folder's other disks, in name order;
  **Options > Disk: < 1 of 2 >** swaps them.
- **D-pad**: joystick by default (port 2; **Joystick port** in the menu). **Options > D-pad**
  switches it to cursor keys and RETURN for BASIC and menus.
- **Reset C64** resets; **Switch off and on** also removes disks and cartridge games.
- No ROM files are needed. SID and MOD files play in the SID player, not in Vice.

## Cartridges (fast loaders)

**Options > Cartridge: < Off >** puts a cartridge such as **Epyx FastLoad** in the expansion
port and leaves it there; Left/Right choose, and the C64 restarts with it. PS64 doesn't
include any (cartridge ROMs are copyrighted): bring your own `.crt` files.

- **Vice**: copy them into `/data/ps64/carts` (Mac: `~/Documents/PS64/carts`). The choice is
  saved and stays through loads; a cartridge game (CRT) takes its place while it runs.
- **C64 Ultimate**: copy them into the Ultimate's `/flash/carts` folder. PS64 sets its
  Cartridge setting and reboots it. That isn't saved to the Ultimate's flash, so switching
  the C64 off takes the cartridge out (save it from the Ultimate's menu to keep it).

## SID: the music player

Plays C64 tunes (.sid files) with the reSIDfp emulation of the 6581 and 8580 chips. Its lists,
switched with Left/Right:

- **Local**: the SID folder, `/data/ps64/sids` (Mac: `~/Documents/PS64/sids`), with USB
  drives at the top.
- **Assembly64**: search for tunes (R1 shows the latest); the results become the playlist.
- **C64 Ultimate** (while connected): the Ultimate's storage.

Cross plays, L1/R1 change the subtune, L2/R2 the tune, Square pauses, Triangle cycles in
order / shuffle / repeat-one, Circle goes home. Tunes end on time (HVSC song lengths; 3
minutes if unknown) and the list plays on. The screen shows the title, author, chip, time,
each voice and an oscilloscope.

With a C64 Ultimate connected, the first row, **Play on**, sends tunes to its real SID chips
instead (the sound comes back through the stream).

## Assembly64, favourites and recents

- **Search Assembly64** (Options): type a name, choose a category with L2 (it starts on
  Games), R2 searches. Cross opens an entry's files; Cross on a file downloads and starts it,
  Square shows other actions. Screenshots of CSDb releases show alongside.
- **Favourites**: Triangle on a title (in the browser, search results, Assembly64, Recents)
  adds or removes it; favourites have a star. **Options > Favourites** has **Games** and
  **SIDs**. A favourite folder or Assembly64 entry starts its first runnable file.
- **Recents**: the last 25 things started, newest first.

## Settings file

PS64 saves your choices in `ps64.cfg` (`/data/ps64/ps64.cfg`, Mac
`~/Documents/PS64/ps64.cfg`), one `key = value` per line. You rarely need to edit it, but
useful keys include:

| Key | What |
| --- | --- |
| `c64`, `password` | The C64 Ultimate to stream from, and its network password |
| `stream-volume`, `emulator-volume`, `sid-volume` | Levels in dB (0 to -30; -12 to start) |
| `home-music` | Home music volume, 0 (off) to 10 |
| `search-roots` | Folders the Ultimate's search index covers |
| `games-dir`, `sids-dir`, `carts-dir` | Other folders for games, tunes, cartridges |
| `dpad` | `keys` makes the D-pad cursor keys in Vice |
| `button-square` (etc.) | A C64 key for a shortcut button |
| `usb-prefix` | Where USB drives are mounted (`none` turns them off) |

## Building, releasing

- [docs/BUILDING.md](docs/BUILDING.md): building for the Mac and the PS5, command-line
  options, testing, source layout.
- [docs/RELEASING.md](docs/RELEASING.md): releases to the Prospero Store.

## Licence and credits

PS64 is free software under the GNU General Public License v3.0 (`LICENSE`). It includes or
builds with:

- [VICE](https://vice-emu.sourceforge.io) (GPL-2.0-or-later), through its
  [libretro core](https://github.com/libretro/vice-libretro) and the
  [PS5 port](https://github.com/mihawk-99/PS5_VICE); `tools/get_vice.sh` names the exact
  source commit. Binaries that include it are distributed under GPL-3.0 with that source.
- [libsidplayfp](https://github.com/libsidplayfp/libsidplayfp) 3.1.1 and
  [libresidfp](https://github.com/libsidplayfp/libresidfp) 1.2.3 (GPL-2.0-or-later).
- Song lengths from the [High Voltage SID Collection](https://hvsc.c64.org)
  (`Songlengths.md5`); no tunes are included.
- SDL2 (zlib licence); on the PS5, the ps5-payload-sdk and the
  [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  (GPL-3.0-or-later), with dlmalloc by Doug Lea (public domain / MIT-0 style).
- Screenshots of CSDb releases from [CSDb](https://csdb.dk), decoded with
  [stb_image](https://github.com/nothings/stb) (public domain / MIT).
- Home screen icons: [Material Icons](https://github.com/google/material-design-icons) by
  Google (Apache 2.0), rasterised with [nanosvg](https://github.com/memononen/nanosvg) (zlib).
- Controller button icons: Kenney's [Input Prompts](https://kenney.nl/assets/input-prompts)
  1.5 (CC0).
- The overlay font includes `font8x8_basic` (public domain).
- Searching uses [Assembly64](https://hackerswithstyle.se) (PS64 sends its own client ID).
