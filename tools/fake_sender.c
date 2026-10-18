/*
 * fake_sender - emit a synthetic VIC data stream for testing ps64 without
 * hardware: a C64-style border and screen, colour bars and a moving raster bar.
 *
 *   fake_sender [host] [port] [--ntsc] [--drop N]
 *
 * --drop N drops roughly one packet in N to exercise loss handling.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define WIDTH 384
#define LINES_PER_PACKET 4

static void wr16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }

static uint8_t pixel(int x, int y, int height, int frame)
{
    int bar = (frame * 2) % height;                 /* moving raster bar */
    if (y >= bar && y < bar + 8)
        return (uint8_t)(1 + ((y - bar) % 15));

    int top = (height - 200) / 2, left = 32;
    if (x < left || x >= left + 320 || y < top || y >= top + 200)
        return 14;                                  /* light blue border */

    int sx = x - left, sy = y - top;
    if (sy < 100)
        return (uint8_t)(sx / 20);                  /* 16 colour bars */
    if (((sx / 8) + (sy / 8) + frame / 25) & 1)
        return 6;                                   /* blue checkerboard */
    return 1;
}

int main(int argc, char **argv)
{
    const char *host = "127.0.0.1";
    int port = 11000, height = 272, drop = 0, positional = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ntsc")) height = 240;
        else if (!strcmp(argv[i], "--drop") && i + 1 < argc) drop = atoi(argv[++i]);
        else if (positional == 0) { host = argv[i]; positional++; }
        else if (positional == 1) { port = atoi(argv[i]); positional++; }
    }
    double fps = height == 272 ? 50.125 : 59.826;

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)port);
    if (s < 0 || inet_pton(AF_INET, host, &dst.sin_addr) != 1) {
        fprintf(stderr, "bad host %s\n", host);
        return 1;
    }
    printf("sending %dx%d @ %.2f fps to %s:%d%s\n", WIDTH, height, fps, host, port,
           drop ? " (with packet loss)" : "");

    uint8_t pkt[12 + WIDTH / 2 * LINES_PER_PACKET];
    uint16_t seq = 0;
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    long frame_ns = (long)(1e9 / fps);

    for (uint16_t frame = 0;; frame++) {
        for (int line = 0; line < height; line += LINES_PER_PACKET) {
            int last = line + LINES_PER_PACKET >= height;
            wr16(pkt + 0, seq++);
            wr16(pkt + 2, frame);
            wr16(pkt + 4, (uint16_t)(line | (last ? 0x8000 : 0)));
            wr16(pkt + 6, WIDTH);
            pkt[8] = LINES_PER_PACKET;
            pkt[9] = 4;
            wr16(pkt + 10, 0);
            uint8_t *p = pkt + 12;
            for (int y = line; y < line + LINES_PER_PACKET; y++)
                for (int x = 0; x < WIDTH; x += 2)
                    *p++ = (uint8_t)(pixel(x, y, height, frame) | (pixel(x + 1, y, height, frame) << 4));
            if (drop && rand() % drop == 0)
                continue;
            sendto(s, pkt, sizeof(pkt), 0, (struct sockaddr *)&dst, sizeof(dst));
        }
        next.tv_nsec += frame_ns;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        {
            struct timespec now, d;
            clock_gettime(CLOCK_MONOTONIC, &now);
            d.tv_sec = next.tv_sec - now.tv_sec;
            d.tv_nsec = next.tv_nsec - now.tv_nsec;
            if (d.tv_nsec < 0) { d.tv_nsec += 1000000000L; d.tv_sec--; }
            if (d.tv_sec >= 0)
                nanosleep(&d, NULL);
        }
    }
}
