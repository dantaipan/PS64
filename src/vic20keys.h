/*
 * vic20keys - PS64's keys (c64keys matrix codes, row * 8 + column) on the VIC-20's
 * keyboard matrix, for the built-in VIC-20. Pure.
 */
#ifndef VIC20KEYS_H
#define VIC20KEYS_H

/* The VIC-20 matrix code (row * 8 + column) of C64 key c64key (0..63), or -1. */
int vic20_key(int c64key);

#endif
