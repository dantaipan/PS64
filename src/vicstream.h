/*
 * vicstream - portable decoder for the Ultimate 64 / C64 Ultimate VIC data stream.
 *
 * No sockets, no SDL: feed it raw UDP payloads, read back a palette-indexed
 * frame. This file is shared by the macOS prototype and the future PS5 port.
 *
 * Packet layout (all multi-byte fields little endian):
 *   0  u16  sequence number
 *   2  u16  frame number
 *   4  u16  line number of first line in packet; bit 15 = last packet of frame
 *   6  u16  pixels per line   (384)
 *   8  u8   lines per packet  (4)
 *   9  u8   bits per pixel    (4)
 *  10  u16  encoding          (0 = raw)
 *  12  ...  pixel data, two 4-bit palette indices per byte
 */
#ifndef VICSTREAM_H
#define VICSTREAM_H

#include <stddef.h>
#include <stdint.h>

#define VIC_HEADER_SIZE   12
#define VIC_MAX_WIDTH     384
#define VIC_MAX_HEIGHT    312   /* full PAL raster; streams normally send 272 (PAL) or 240 (NTSC) */
#define VIC_DEFAULT_PORT  11000

typedef struct {
    uint16_t seq;
    uint16_t frame;
    uint16_t line;
    int      last;          /* last packet of the frame */
    uint16_t width;
    uint8_t  lines;
    uint8_t  bpp;
    uint16_t encoding;
} vic_header;

typedef struct {
    uint64_t packets;
    uint64_t bad_packets;   /* malformed or unsupported */
    uint64_t lost_packets;  /* gaps in the sequence number */
    uint64_t frames;
} vic_stats;

typedef struct {
    /* Completed frame, one palette index (0-15) per pixel. */
    uint8_t  pixels[VIC_MAX_WIDTH * VIC_MAX_HEIGHT];
    int      width;
    int      height;

    /* Frame being assembled. */
    uint8_t  work[VIC_MAX_WIDTH * VIC_MAX_HEIGHT];
    int      work_frame;    /* -1 = none */
    int      work_height;

    int      have_seq;
    uint16_t next_seq;
    int      swap_nibbles;  /* 0: low nibble is the left pixel */

    vic_stats stats;
} vic_decoder;

/* 16-colour C64 palette as 0xAARRGGBB. */
extern const uint32_t vic_palette[16];

void vic_init(vic_decoder *d);

/* Parse a header; returns 0 on success, -1 if the packet is too short. */
int vic_parse_header(const uint8_t *buf, size_t len, vic_header *h);

/*
 * Feed one UDP payload. Returns 1 when a frame was completed and copied to
 * d->pixels / d->width / d->height, 0 otherwise.
 */
int vic_feed(vic_decoder *d, const uint8_t *buf, size_t len);

/* Convert the completed frame to 32-bit pixels; pitch is in bytes. */
void vic_to_argb(const vic_decoder *d, uint32_t *dst, int pitch);

#endif
