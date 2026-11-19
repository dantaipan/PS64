# PS64

Play the Commodore 64 on a jailbroken PS5 (or a Mac), with or without the real hardware.

- **Stream**: show a **Commodore 64 Ultimate**'s picture and sound and drive it from the
  DualSense: on-screen keyboard, file browser, search, favourites.
- **VICE C64**: no C64? PS64's **built-in emulator** (VICE) runs one. Copy in games or download
  them from Assembly64.
- **SID**: a **music player** for C64 tunes, on PS64 itself or on the Ultimate's SID chips.
- **VICE VIC20**: the C64's little brother, built in too. Copy in its games.

Both machines can search the online **Assembly64** database and start what you find straight
away.

*The emulators in PS64 are deliberately simple. For something more feature-rich,
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

PS64 starts on a 3D home screen, a carousel: **SID** (the music player), **VICE C64** (the
emulator, in the middle) and **Stream** (the C64 Ultimate, shown live on the monitor once
found), with **VICE VIC20** (the old TV) waiting behind. Left/Right turn it (it goes round),
Cross starts the one in the middle. **Options > Home** goes back there from a machine.

- **Quit**: hold Circle (a ring fills round it; on the Mac, hold Esc or press Cmd+Q).
- **Settings** (Options): the C64 Ultimates found (pick one, enter an
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
| Right stick, R1 / L1 | The mouse and its left / right buttons (when **Mouse** is on) |
| Options | PS64's menu |

**On-screen keyboard**: D-pad moves, Cross types, Circle closes, Square DEL, Triangle SPACE,
R2 RETURN, L1 Shift (twice locks), R1 C=, L2 CTRL. Keys show what they'll type with the
current modifiers. The shortcut buttons can be changed (`button-square = f2`, etc.). Their keys
are held down while you hold the button (on a C64 Ultimate, with firmware 1.2.0 or newer).

**Mouse** (Options > Mouse, Cmd+U on the Mac): a 1351 mouse in port 1, for GEOS, C64 OS and
other mouse software, on the VICE C64 and on a C64 Ultimate with firmware 1.2.1 or newer. The
right stick moves it, R1 and L1 are its buttons, and the joystick moves to port 2 meanwhile.
On the Mac, click the picture to use the Mac's own mouse (Esc or Cmd lets go). On the VICE
C64, the joystick's fire also clicks the mouse's left button.

**The menu (Options)**: Home, Resume, browse and search files, Search Assembly64, Favourites,
Recents, Disk (multi-disk games), Reset C64, Reboot / Switch off and on, D-pad mode (emulators),
Cartridge, SuperCPU, REU and JiffyDOS (VICE C64), Audio level (Left/Right; Cross mutes), Joystick port,
Mouse, Update search index, About, Quit. On screens with lists, the hint line at the bottom shows the buttons.

**On the Mac**, any SDL-supported controller works, and so does the keyboard on its own:

- **Typing**: in a machine the Mac keyboard is the C64's. Keys are held as you hold them in
  the built-in machines and on a C64 Ultimate with firmware 1.2.0 or newer (so they work in
  games); on older firmware they're typed into its keyboard buffer. Esc is RUN/STOP, Option is C=, Control is CTRL, PgUp (fn+Up) is RESTORE,
  Home (fn+Left) is CLR/HOME, F1-F8 the C64's function keys (fn+F1... on a laptop keyboard).
- **Joystick**: **Cmd+J** turns the arrows into the joystick, with Space or Right Shift as
  fire (other keys still type); Cmd+J again for typing. Starting a game turns it on.
- **PS64's keys**: **Cmd+O** menu (on the home screen: Settings), **Cmd+B** file browser,
  **Cmd+K** on-screen keyboard, **Cmd+U** mouse, **Cmd+F** fullscreen, **Cmd+M** mute,
  **Cmd+Q** quit. In PS64's
  screens: arrows, Return, Esc, Space, PgUp/PgDn, Tab, F (favourite) and L.
- With no controller connected, the hints show these keys instead of controller buttons.

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
- **Keys and joystick**: with firmware 1.2.0 or newer, PS64 sends real key presses (held as
  long as you hold them), the joystick and RESTORE; 1.2.1 adds the mouse. PS64 finds out what
  the firmware can do when it connects. On firmware 1.1.0, keys go into the C64's keyboard
  buffer, which works in BASIC, loaders and most menus but not in games that read the
  keyboard directly, and there's no joystick.
- **The Ultimate's menu** (R3): with firmware 1.2.0 or newer, PS64 notices when it's open,
  and the D-pad moves through it, Cross selects and Circle goes back.

## VICE C64: the built-in emulator

A C64 inside PS64: keyboard and joystick work in games, multi-disk games swap disks, and
disks load at full speed. **Options > Home** pauses it; **VICE C64** carries on where it was.

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
- **Options > SuperCPU: < On >** gives the C64 a SuperCPU (a 20 MHz 65816 with 16 MB of
  SuperRAM) for software that needs one; it restarts the C64 from BASIC, with the same games
  folder. The Cartridge item is hidden then (fast loaders don't work at 20 MHz).
- **Options > REU: < 512 KB >** fits a RAM Expansion Unit (Off, 512 KB like the 1750, or 16
  MB), with or without the SuperCPU; changing it restarts the C64. Software that comes with
  an REU image loads it when the `.reu` file has the program's name (`game.d64` and
  `game.reu`). Cartridge games run without the REU.
- Both choices are saved (`emu-supercpu`, `emu-reu` in `ps64.cfg`).
- No ROM files are needed, except for JiffyDOS (below). SID and MOD files play in the SID
  player, not in the emulator.

### JiffyDOS

**Options > JiffyDOS: < On >** gives the C64 and its disk drive JiffyDOS, which loads several
times faster from games that use the normal LOAD (many have their own fast loader, which
stays as it is). PS64 doesn't include it (JiffyDOS is sold by Retro Innovations): copy your
own ROM files into `/data/ps64/roms` (Mac: `~/Documents/PS64/roms`), named as you like, in
folders if you like. The item appears once the ROMs are there, and the choice is saved.

| ROM | For |
| --- | --- |
| C64 kernal (8 KB) | The C64: needed, with a drive ROM |
| 1541-II (16 KB) | D64 and G64 disks |
| 1581 (32 KB) | D81 disks |
| 1571 (32 KB) | D71 disks |
| CMD's SuperCPU ROM (64 KB or more) | The SuperCPU: it has JiffyDOS built in (VICE's own SuperCPU ROM hasn't), with a drive ROM |

A drive without its JiffyDOS ROM runs the ordinary one. JiffyDOS is left out for tapes, PRG
files and cartridge games, which don't load through the drive.

## VICE VIC20

**VICE VIC20** on the home screen (turn the carousel to the TV) is a second built-in emulator,
VICE's VIC-20. It works like VICE C64 (keyboard, joystick, the menu, browse and search,
favourites), with its own folder and without Assembly64 (which has C64 software only). The two
each wait where they were while you use the other.

- **Games folder**: `/data/ps64/vic20` (Mac: `~/Documents/PS64/vic20`), with USB drives at the
  top. It runs PRG, D64, TAP, T64 and cartridge images (`.crt`, `.20`, `.40`, `.60`, `.a0`,
  `.b0`).
- **Memory expansion** is chosen from the program's size. To force one, put it in the file or
  folder name: `(8k)`, `[16k]`, or a folder called `24k`.
- One joystick port, so no **Joystick port** in its menu.

## Cartridges (fast loaders)

**Options > Cartridge: < Off >** puts a cartridge such as **Epyx FastLoad** in the expansion
port and leaves it there; Left/Right choose, and the C64 restarts with it. PS64 doesn't
include any (cartridge ROMs are copyrighted): bring your own `.crt` files.

- **VICE C64**: copy them into `/data/ps64/carts` (Mac: `~/Documents/PS64/carts`). The choice is
  saved and stays through loads; a cartridge game (CRT) takes its place while it runs.
- **C64 Ultimate**: copy them into the Ultimate's `/flash/carts` folder. PS64 sets its
  Cartridge setting and reboots it. That isn't saved to the Ultimate's flash, so switching
  the C64 off takes the cartridge out (save it from the Ultimate's menu to keep it).

## SID: the music player

Plays C64 tunes (.sid files) with the reSIDfp emulation of the 6581 and 8580 chips. Its lists,
switched with Left/Right:

- **Local**: the SID folder, `/data/ps64/sids` (Mac: `~/Documents/PS64/sids`), with USB
  drives at the top.
- **Assembly64**: search for tunes by title or musician (R1 shows the latest); the results
  become the playlist.
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
| `games-dir`, `vic20-dir`, `sids-dir`, `carts-dir`, `roms-dir` | Other folders for games, VIC-20 games, tunes, cartridges, JiffyDOS ROMs |
| `dpad`, `vic20-dpad` | `keys` makes the D-pad cursor keys in VICE C64 (and the stream) or VICE VIC20 |
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
  [PS5 port](https://github.com/mihawk-99/PS5_VICE), for the C64 and the VIC-20 (their ROMs
  come with VICE); `tools/get_vice.sh` names the exact
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
- Controller button icons: Kenney's [Input Prompts](https://kenney.nl/assets/input-prompts)
  1.5 (CC0), rasterised with [nanosvg](https://github.com/memononen/nanosvg) (zlib).
- The overlay font includes `font8x8_basic` (public domain).
- Searching uses [Assembly64](https://hackerswithstyle.se) (PS64 sends its own client ID).
