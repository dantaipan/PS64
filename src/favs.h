/*
 * favs - favourites and recents: titles to start again, from the C64's own
 * storage (files or game folders) or from Assembly64. Kept in small
 * tab-separated text files next to the config.
 */
#ifndef FAVS_H
#define FAVS_H

#include <stddef.h>

/* LOCAL_* are on the C64 Ultimate's storage, EMU_* in the built-in emulator's
 * games folder; Assembly64 entries work on both. */
enum { FAV_LOCAL_FILE, FAV_LOCAL_FOLDER, FAV_A64_FILE, FAV_A64_ITEM, FAV_EMU_FILE, FAV_EMU_FOLDER };
enum { FAV_GAMES, FAV_SIDS };

#define RECENTS_MAX 25

typedef struct {
    int  kind, group;
    int  action;              /* launch action (FA_*), or FA_NONE for the default */
    char name[96];            /* shown */
    char path[512];           /* local path, or the Assembly64 file name */
    char item_id[64];         /* Assembly64 */
    int  category, file_id;   /* Assembly64 */
} fav_entry;

typedef struct {
    fav_entry *e;
    int        count, cap;
    int        max;           /* 0 = no limit */
} fav_list;

void fav_init(fav_list *l, int max);
void fav_free(fav_list *l);

/* Index of the entry with the same identity as e, or -1. */
int  fav_find(const fav_list *l, const fav_entry *e);
/* Add e, or remove it if present; returns 1 if added, 0 if removed. */
int  fav_toggle(fav_list *l, const fav_entry *e);
void fav_remove(fav_list *l, int index);
/* Put e first (moving an existing copy), trimming to max. */
void fav_add_recent(fav_list *l, const fav_entry *e);

int  fav_count_group(const fav_list *l, int group);
/* Index in l of the nth entry of group, or -1. */
int  fav_nth_in_group(const fav_list *l, int group, int n);

int  fav_load(fav_list *l, const char *file);
int  fav_save(const fav_list *l, const char *file);

/* Entry for a path on the machine's storage (named "<folder>/<file>" or the
 * folder): the Ultimate's, or the games folder in emulator mode (files_emulator()). */
fav_entry fav_local(const char *path, int is_dir);
/* A file or folder path (not Assembly64), and whether it's the emulator's. */
int  fav_is_path(const fav_entry *e);
int  fav_is_folder(const fav_entry *e);
int  fav_is_emulator(const fav_entry *e);
/* Entry for one file of an Assembly64 item. */
fav_entry fav_a64_file(const char *item_id, int category, int file_id, const char *file_name,
                       const char *item_name);

/* Group for a file name by type: SID/MOD are SIDs, everything else games. */
int  fav_group_for_name(const char *name);
/* Group for an Assembly64 category key/name ("music", "sid" -> SIDs). */
int  fav_group_for_category(const char *key_or_name);

#endif
