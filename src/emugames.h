/*
 * emugames - starting things in the built-in emulator: files and folders
 * from the games folder, and Assembly64 downloads. A disk image is started
 * together with the other disks next to it (an .m3u list), so the menu can
 * swap to Disk 2 later. Built only with PS64_EMU.
 */
#ifndef EMUGAMES_H
#define EMUGAMES_H

#include "a64.h"

#include <stddef.h>

#define EG_MAX_DISKS 16

enum { EG_ERROR = -1, EG_WAITING = 0, EG_STARTED = 1 };

typedef struct {
    char games_dir[512];      /* "/" in the browser */
    char cache_dir[512];      /* Assembly64 downloads and the disk list */

    /* Assembly64 files being fetched for one start. */
    int      active;
    char     item_id[64], item_name[96];
    int      category, action;
    a64_file files[EG_MAX_DISKS];
    int      nfiles, next;
    char     saved[EG_MAX_DISKS][600];
} emugames;

void eg_init(emugames *g, const char *games_dir, const char *cache_dir);

/*
 * Start path (inside the games folder) with action (FA_*, or FA_NONE for the
 * file's default; FA_LAUNCH_FOLDER for a folder). msg gets a line for a
 * banner. Returns EG_STARTED or EG_ERROR.
 */
int eg_launch_path(emugames *g, const char *path, int action, char *msg, size_t cap);

/*
 * Start file `chosen` of an Assembly64 item (its file list known): fetches
 * what isn't cached yet through a, then starts. Returns EG_WAITING (feed the
 * downloads to eg_a64_file), EG_STARTED (all cached) or EG_ERROR.
 */
int eg_a64_begin(emugames *g, a64 *a, const char *item_id, int category, const char *item_name,
                 const a64_file *files, int nfiles, int chosen, int action, char *msg, size_t cap);

/*
 * A finished download (A64R_FILE). Returns EG_WAITING while more are needed,
 * EG_STARTED, or EG_ERROR; returns EG_ERROR without touching r and with an
 * empty msg if no Assembly64 start is in progress.
 */
int eg_a64_file(emugames *g, a64 *a, const a64_result *r, char *msg, size_t cap);

/* Cancel an Assembly64 start in progress. */
void eg_a64_cancel(emugames *g);

#endif
