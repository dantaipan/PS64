/*
 * audiostream - portable decoder for the Ultimate 64 / C64 Ultimate audio data stream.
 *
 * Packet layout:
 *   0  u16  sequence number (little endian)
 *   2  ...  192 stereo frames, signed 16-bit little endian, interleaved L/R
 *
 * Sample rate is about 47983 Hz on PAL machines (measured 47986 Hz on a C64
 * Ultimate). Like vicstream, this file has no OS or SDL dependencies.
 */
#ifndef AUDIOSTREAM_H
#define AUDIOSTREAM_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_DEFAULT_PORT   11001
#define AUDIO_HEADER_SIZE    2
#define AUDIO_RATE_PAL       47983
#define AUDIO_MAX_FRAMES     512   /* per packet; streams send 192 */

typedef struct {
    uint64_t packets;
    uint64_t bad_packets;
    uint64_t lost_packets;
} audio_stats;

typedef struct {
    int       have_seq;
    uint16_t  next_seq;
    audio_stats stats;
} audio_decoder;

void audio_init(audio_decoder *d);

/*
 * Decode one UDP payload into host-endian interleaved stereo samples.
 * out must hold 2 * max_frames samples. Returns the number of stereo frames
 * written, or 0 for a malformed packet.
 */
size_t audio_feed(audio_decoder *d, const uint8_t *buf, size_t len,
                  int16_t *out, size_t max_frames);

#endif
