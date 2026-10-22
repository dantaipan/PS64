/*
 * playlist - the SID player's play order over a list of tunes: in order,
 * shuffled (a fixed random order from a seed, so "previous" goes back), or
 * repeating one tune. Pure; the list itself (files, Assembly64 results)
 * lives with the caller, which plays entry playlist_current().
 */
#ifndef PLAYLIST_H
#define PLAYLIST_H

#define PLAYLIST_MAX 4096

enum { PLAY_IN_ORDER, PLAY_SHUFFLE, PLAY_REPEAT_ONE, PLAY_MODES };

typedef struct {
    int      count;               /* entries in the list */
    int      mode;                /* PLAY_* */
    int      pos;                 /* position in order[] */
    int      order[PLAYLIST_MAX]; /* entry index at each position */
    unsigned seed;
} playlist;

/* Start on entry start (0..count-1) of count entries. */
void playlist_start(playlist *p, int count, int start, int mode, unsigned seed);
int  playlist_current(const playlist *p);   /* -1 when empty */

/* The entry after (next) or before (prev) the current one, made current; -1 at the
 * end of the list (in order and shuffled the list wraps only when wrap is set). At the
 * end of a tune, repeat-one replays it; skipping by hand always moves on. */
int  playlist_next(playlist *p, int by_hand, int wrap);
int  playlist_prev(playlist *p);

/* Change mode, keeping the current entry (a new shuffle order starts from it). */
void playlist_set_mode(playlist *p, int mode);

#endif
