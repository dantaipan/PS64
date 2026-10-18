/*
 * c64files - browse the Ultimate's storage (FTP) and start files (REST) on
 * a worker thread, so the video loop never waits on the network.
 */
#ifndef C64FILES_H
#define C64FILES_H

#include "c64remote.h"
#include "ftp.h"

enum { FT_DIR, FT_PRG, FT_DISK, FT_CRT, FT_SID, FT_MOD, FT_OTHER };

enum {
    FA_NONE = -1,
    FA_RUN,          /* .prg: load and run */
    FA_LOAD,         /* .prg: load only */
    FA_MOUNT_RUN,    /* disk: reboot, mount on A, LOAD"*",8,1 + RUN */
    FA_MOUNT_A,
    FA_MOUNT_B,
    FA_RUN_CRT,
    FA_PLAY,         /* .sid / .mod */
    FA_RESET,        /* machine actions (no path): menu items */
    FA_REBOOT,
    FA_LAUNCH_FOLDER,/* folder: start its first runnable file (files_pick_launch) */
    FA_COUNT
};

int  files_type(const char *name, int is_dir);
int  files_default_action(int type);
/* Actions that apply to a file type; returns how many were written. */
int  files_actions_for(int type, int *out, int max);
const char *files_action_name(int action);
const char *files_type_tag(int type);

/*
 * Which file of a folder to start: disk images first, then PRG, CRT, SID,
 * MOD; within a type the first by name (so Disk1.d64 before Disk2.d64).
 * Returns the index into names, or -1 if none is runnable.
 */
int  files_pick_launch(const char *const *names, int n);

/* Find "READY." in 1000 bytes of screen memory (screen codes). */
int  files_screen_ready(const unsigned char *screen, int len);

enum { FR_NONE, FR_LISTING, FR_DONE, FR_ERROR };

typedef struct {
    int        kind;
    char       path[512];        /* folder listed, or file acted on */
    char       message[160];
    ftp_entry *entries;          /* FR_LISTING: owned by the caller after poll */
    int        count;
} files_result;

typedef struct c64files c64files;

c64files *files_start(const char *host, const char *password, remote *rem);
void files_stop(c64files *f);
void files_list(c64files *f, const char *path);
void files_action(c64files *f, const char *path, int action);
/* Run an action on file bytes uploaded with the request (takes ownership of
 * data); name gives the type, e.g. "Boulder Dash.d64". */
void files_action_data(c64files *f, const char *name, unsigned char *data, size_t len, int action);
int  files_busy(c64files *f);
/* Returns 1 and fills res when a request has finished (free res->entries). */
int  files_poll(c64files *f, files_result *res);

#endif
