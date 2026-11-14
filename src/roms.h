/*
 * roms - JiffyDOS ROMs for the built-in VICE C64, from the user's own files.
 *
 * PS64 can't include them (they're sold by Retro Innovations), so people copy theirs into
 * the roms folder, named as they like: each file is recognised by its size and text, and
 * copied into the core's system folder under the names VICE looks for. JiffyDOS needs one
 * at each end: the C64's kernal (or the SuperCPU's ROM, which has it built in) and the
 * drive's (1541-II for D64, 1571 for D71, 1581 for D81). A drive without its ROM runs the
 * stock one (tools/get_vice.sh patches the core for that).
 */
#ifndef ROMS_H
#define ROMS_H

#include <stddef.h>

enum {
    ROM_C64  = 1,     /* JiffyDOS C64 kernal, 8 KB */
    ROM_1541 = 2,     /* JiffyDOS 1541-II drive ROM, 16 KB */
    ROM_1571 = 4,     /* JiffyDOS 1571, 32 KB */
    ROM_1581 = 8,     /* JiffyDOS 1581, 32 KB */
    ROM_SCPU = 16,    /* CMD's SuperCPU ROM (JiffyDOS built in), 64 KB or more */
};

/* Which JiffyDOS ROM data is (file name: a hint for the SuperCPU's): one ROM_*, or 0. */
int roms_identify(const char *name, const unsigned char *data, size_t len);

/*
 * Copy the ROMs found in dir (and its folders) into vice_dir (the core's "system/vice")
 * under VICE's names, and take out ones no longer there. Returns the ROM_* found.
 */
int roms_install(const char *dir, const char *vice_dir);

/* JiffyDOS can be on with these ROMs: on the plain C64, or on the SuperCPU. */
int roms_jiffydos(int found, int supercpu);

#endif
