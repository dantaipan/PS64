/*
 * discover - find Ultimate devices on the local /24 by probing
 * GET /v1/info on every host with port 80 open, on a background thread.
 */
#ifndef DISCOVER_H
#define DISCOVER_H

#define DISCOVER_MAX 16

typedef struct {
    char ip[16];
    char hostname[64];
    char product[48];
    char firmware[24];
    int  locked;           /* answered 403: network password set */
} c64_device;

typedef struct discover discover;

/* also_near: an IP whose /24 is scanned too (e.g. the saved C64), or NULL. */
discover *discover_start(const char *also_near, const char *password);

/* Copies results found so far; returns 1 when the scan has finished.
 * progress is 0-100. */
int discover_poll(discover *d, c64_device *out, int *count, int *progress);

void discover_stop(discover *d);

/* Fill dev from a /v1/info body; returns 0 if it looks like an Ultimate. */
int discover_parse_info(const char *body, c64_device *dev);

#endif
