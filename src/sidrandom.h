/*
 * sidrandom - a random .sid from the SID folder and its subfolders, for the home
 * screen's music. The folders are walked on a thread (a big collection takes a
 * while on the PS5) and one tune is picked on the way, each as likely as any other
 * (reservoir sampling: no list is kept). USB drives aren't searched.
 */
#ifndef SIDRANDOM_H
#define SIDRANDOM_H

#include <stddef.h>

#define SIDRANDOM_MAX_DIRS 5000   /* folders looked in at most */

typedef struct sidrandom sidrandom;

/* Start looking in root (a real folder). NULL if the thread didn't start. */
sidrandom *sidrandom_start(const char *root, unsigned seed);
/* 0 while looking; 1 with the tune's path in the folder ("/Hubbard/Commando.sid");
 * -1 if there's none. */
int  sidrandom_poll(sidrandom *r, char *path, size_t cap);
/* Stop looking (if it still is) and free it. */
void sidrandom_free(sidrandom *r);

/* The walk itself (the thread runs this): 1 and the path, or 0 for no tune. Stops
 * early when *stop becomes set. */
int  sidrandom_pick(const char *root, unsigned seed, int max_dirs, const volatile int *stop,
                    char *out, size_t cap);
/* Reservoir sampling: whether candidate n (1-based) takes the place of the one kept. */
int  sidrandom_take(unsigned *state, int n);

#endif
