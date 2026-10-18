/*
 * c64index - a searchable list of the runnable files on an Ultimate's
 * storage (PRG, disk images, CRT, SID, MOD), or of the emulator's games
 * folder. Built by walking folders (over FTP, or locally) on a worker
 * thread, saved to a text file, searched by keyword.
 */
#ifndef C64INDEX_H
#define C64INDEX_H

#include <stddef.h>

#define INDEX_MAX_FILES 200000
#define INDEX_MAX_DEPTH 10

typedef struct {
    char               path[256];
    unsigned long long size;
    int                type;      /* files_type() */
    int                score;
} index_hit;

typedef struct {
    int  building, done, cancelled;
    int  dirs, files, failed_dirs;
    char current[256];
    char error[128];
} index_status;

typedef struct c64index c64index;

c64index *index_new(void);
void index_free(c64index *x);          /* stops a running build first */

/* Load a saved index; returns 0 if it exists and was built for host. */
int  index_load(c64index *x, const char *file, const char *host);
int  index_save(c64index *x, const char *file, const char *host, const char *roots);

/* Start (re)building from comma-separated roots, e.g. "/SD,/USB0"; saves to
 * file when finished. Searches work on partial results meanwhile. */
void index_build_start(c64index *x, const char *host, const char *password,
                       const char *roots, const char *file);
/* The same for a local folder (the emulator's games folder; paths start at
 * "/" inside root). Saved with host INDEX_LOCAL_HOST. */
#define INDEX_LOCAL_HOST "emulator"
void index_build_local_start(c64index *x, const char *root, const char *file);
void index_status_get(c64index *x, index_status *st);
int  index_count(c64index *x);

/* Adds one file (used by the builder; exposed for tests). */
int  index_add(c64index *x, const char *path, unsigned long long size);

/*
 * Case-insensitive keyword search: every word must occur in the path.
 * Matches in the file name rank above folder-only matches, whole-word
 * matches above partial ones, then by file name. Returns hits written.
 */
int  index_search(c64index *x, const char *query, index_hit *out, int max);

#endif
