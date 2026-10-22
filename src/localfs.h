/*
 * localfs - the built-in emulator's own folders: the games folder people
 * fill over FTP, and the cache that Assembly64 downloads are saved to.
 *
 * Paths inside the games folder are written like the C64 Ultimate's,
 * "/Folder/Game.d64", with "/" being the games folder itself, so the file
 * browser, search index and favourites work on them unchanged. USB drives
 * plugged into the PS5 appear at the top as folders USB0..USB7, like the
 * Ultimate's ("/USB0/Games/Elite.d64").
 */
#ifndef LOCALFS_H
#define LOCALFS_H

#include "ftp.h"

#include <stddef.h>

#define LOCALFS_USB_DRIVES 8

/* USB drives: drive n is at prefix + n (the PS5: "/mnt/usb"); NULL, "" or "none" for none. */
void localfs_set_usb(const char *prefix);
/* Real folder of USB drive n if it's there (plugged in and mounted), else NULL. */
const char *localfs_usb_drive(int n, char *out, size_t cap);

/* Real path of path ("/..." inside root, or "/USBn/..." on a USB drive). ".." parts are dropped. */
void localfs_real_path(const char *root, const char *path, char *out, size_t cap);

/*
 * List path inside root: folders first, then by name, hidden files left out; at "/" the
 * USB drives plugged in come first (USB0...). Returns the count, or -1 with a message in err.
 */
int  localfs_list(const char *root, const char *path, ftp_entry *out, int max,
                  char *err, size_t errcap);

/* mkdir -p, the folders open to everyone (0777: filled over FTP as another user). 0 or -1. */
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
