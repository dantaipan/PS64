/* Unit tests for the portable decoder: make test */
#include "../src/audiostream.h"
#include "../src/vicstream.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static size_t make_packet(uint8_t *p, uint16_t seq, uint16_t frame, int line, int last, uint8_t fill)
{
    memset(p, 0, 12);
    p[0] = seq & 0xFF;   p[1] = seq >> 8;
    p[2] = frame & 0xFF; p[3] = frame >> 8;
    uint16_t l = (uint16_t)(line | (last ? 0x8000 : 0));
    p[4] = l & 0xFF;     p[5] = l >> 8;
    p[6] = 384 & 0xFF;   p[7] = 384 >> 8;
    p[8] = 4;
    p[9] = 4;
    memset(p + 12, fill, 768);
    return 12 + 768;
}

static vic_decoder d;

int main(void)
{
    uint8_t pkt[780];

    /* Full PAL frame assembles and reports its height. */
    vic_init(&d);
    int done = 0;
    for (int line = 0; line < 272; line += 4)
        done = vic_feed(&d, pkt, make_packet(pkt, (uint16_t)(line / 4), 7, line, line == 268, 0x21));
    CHECK(done == 1);
    CHECK(d.width == 384 && d.height == 272);
    CHECK(d.pixels[0] == 1 && d.pixels[1] == 2);              /* low nibble first */
    CHECK(d.stats.frames == 1 && d.stats.lost_packets == 0);

    uint32_t argb[384 * 272];
    vic_to_argb(&d, argb, 384 * 4);
    CHECK(argb[0] == vic_palette[1] && argb[1] == vic_palette[2]);

    /* Nibble swap. */
    vic_init(&d);
    d.swap_nibbles = 1;
    vic_feed(&d, pkt, make_packet(pkt, 0, 0, 0, 1, 0x21));
    CHECK(d.pixels[0] == 2 && d.pixels[1] == 1);

    /* NTSC height, sequence wrap-around without false losses. */
    vic_init(&d);
    uint16_t seq = 65530;
    for (int line = 0; line < 240; line += 4)
        vic_feed(&d, pkt, make_packet(pkt, seq++, 1, line, line == 236, 0x00));
    CHECK(d.height == 240 && d.stats.lost_packets == 0);

    /* Lost final packet: the next frame number completes the previous frame. */
    vic_init(&d);
    vic_feed(&d, pkt, make_packet(pkt, 0, 1, 0, 0, 0x33));
    vic_feed(&d, pkt, make_packet(pkt, 1, 1, 4, 0, 0x33));
    done = vic_feed(&d, pkt, make_packet(pkt, 5, 2, 0, 0, 0x44));   /* seq 2-4 lost */
    CHECK(done == 1 && d.stats.frames == 1 && d.height == 8);
    CHECK(d.stats.lost_packets == 3);

    /* Malformed packets are rejected. */
    vic_init(&d);
    CHECK(vic_feed(&d, pkt, 10) == 0);
    make_packet(pkt, 0, 0, 0, 1, 0);
    pkt[9] = 8;                                               /* unsupported bpp */
    CHECK(vic_feed(&d, pkt, 780) == 0);
    make_packet(pkt, 0, 0, 310, 1, 0);                         /* runs past max height */
    CHECK(vic_feed(&d, pkt, 780) == 0);
    CHECK(vic_feed(&d, pkt, 100) == 0);                        /* truncated payload */
    CHECK(d.stats.bad_packets == 4 && d.stats.packets == 0);

    /* Audio: 192 stereo frames, little-endian samples, sequence gaps. */
    static audio_decoder ad;
    static int16_t out[AUDIO_MAX_FRAMES * 2];
    uint8_t apkt[2 + 192 * 4] = {0};
    audio_init(&ad);
    apkt[0] = 0xFF; apkt[1] = 0xFF;                            /* seq 65535 */
    apkt[2] = 0x34; apkt[3] = 0x12;                            /* L = 0x1234 */
    apkt[4] = 0xFE; apkt[5] = 0xFF;                            /* R = -2 */
    CHECK(audio_feed(&ad, apkt, sizeof(apkt), out, AUDIO_MAX_FRAMES) == 192);
    CHECK(out[0] == 0x1234 && out[1] == -2);
    apkt[0] = 0; apkt[1] = 0;                                  /* wraps to 0: no loss */
    audio_feed(&ad, apkt, sizeof(apkt), out, AUDIO_MAX_FRAMES);
    apkt[0] = 3;                                               /* 1 and 2 lost */
    audio_feed(&ad, apkt, sizeof(apkt), out, AUDIO_MAX_FRAMES);
    CHECK(ad.stats.packets == 3 && ad.stats.lost_packets == 2);
    CHECK(audio_feed(&ad, apkt, 1, out, AUDIO_MAX_FRAMES) == 0);          /* too short */
    CHECK(audio_feed(&ad, apkt, 2 + 5, out, AUDIO_MAX_FRAMES) == 0);      /* partial frame */
    CHECK(audio_feed(&ad, apkt, sizeof(apkt), out, 100) == 0);            /* too many frames */
    CHECK(ad.stats.bad_packets == 3);

    printf(failures ? "%d test(s) failed\n" : "all tests passed\n", failures);
    return failures != 0;
}
