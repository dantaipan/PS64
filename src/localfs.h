/*
 * localfs - the built-in emulator's own folders: the games folder people
 * fill over FTP, and the cache that Assembly64 downloads are saved to.
 *
 * Paths inside the games folder are written like the C64 Ultimate's,
 * "/Folder/Game.d64", with "/" being the games folder itself, so the file
 * browser, search index and favourites work on them unchanged.
 */
#ifndef LOCALFS_H
#define LOCALFS_H

#include "ftp.h"

#include <stddef.h>

/* Real path of path ("/..." inside root). ".." parts are dropped. */
void localfs_real_path(const char *root, const char *path, char *out, size_t cap);

/*
 * List path inside root: folders first, then by name, hidden files left out.
 * Returns the count, or -1 with a message in err.
 */
int  localfs_list(const char *root, const char *path, ftp_entry *out, int max,
                  char *err, size_t errcap);

/*
 * Where a native PS5 app can't list folders (see localfs_list), write
 * games.txt in root by walking it over the console's own FTP server
 * (127.0.0.1:2121). Does nothing (returns 0) if root can be listed
 * directly. Returns the number of files listed, or -1 with a message in err.
 */
int  localfs_refresh_manifest(const char *root, char *err, size_t errcap);

/* mkdir -p. 0 or -1. */
int  localfs_mkdirs(const char *dir);
/* Write a file, creating its folder. 0 or -1. */
int  localfs_write(const char *file, const void *data, size_t len);

/* A file name safe to save under: no slashes or control characters, not empty. */
void localfs_safe_name(const char *name, char *out, size_t cap);

/*
 * The disk images to offer for swapping when names[chosen] is started:
 * the disk images among names, ordered by name, starting with the chosen
 * one and wrapping round (Disk2 chosen of 3 -> 2, 3, 1). Writes indexes
 * into names to order; returns the count (0 if names[chosen] isn't a disk).
 */
int  localfs_disk_set(const char *const *names, int n, int chosen, int *order, int max);

/* Write an .m3u disk list (one path per line). 0 or -1. */
int  localfs_write_m3u(const char *file, const char *const *paths, int n);

#endif
