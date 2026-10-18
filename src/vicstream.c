#include "vicstream.h"

#include <string.h>

/* "Colodore" palette, close to the Ultimate 64 default. */
const uint32_t vic_palette[16] = {
    0xFF000000, 0xFFFFFFFF, 0xFF813338, 0xFF75CEC8,
    0xFF8E3C97, 0xFF56AC4D, 0xFF2E2C9B, 0xFFEDF171,
    0xFF8E5029, 0xFF553800, 0xFFC46C71, 0xFF4A4A4A,
    0xFF7B7B7B, 0xFFA9FF9F, 0xFF706DEB, 0xFFB2B2B2,
};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void vic_init(vic_decoder *d)
{
    memset(d, 0, sizeof(*d));
    d->width = VIC_MAX_WIDTH;
    d->height = 272;
    d->work_frame = -1;
}

int vic_parse_header(const uint8_t *buf, size_t len, vic_header *h)
{
    if (len < VIC_HEADER_SIZE)
        return -1;
    uint16_t line = rd16(buf + 4);
    h->seq      = rd16(buf + 0);
    h->frame    = rd16(buf + 2);
    h->line     = line & 0x7FFF;
    h->last     = (line & 0x8000) != 0;
    h->width    = rd16(buf + 6);
    h->lines    = buf[8];
    h->bpp      = buf[9];
    h->encoding = rd16(buf + 10);
    return 0;
}

static void publish(vic_decoder *d)
{
    if (d->work_height > 0) {
        d->height = d->work_height;
        memcpy(d->pixels, d->work, (size_t)d->width * (size_t)d->height);
        d->stats.frames++;
    }
}

int vic_feed(vic_decoder *d, const uint8_t *buf, size_t len)
{
    vic_header h;
    if (vic_parse_header(buf, len, &h) != 0 || h.encoding != 0 || h.bpp != 4 ||
        h.width == 0 || h.width > VIC_MAX_WIDTH || (h.width & 1) || h.lines == 0) {
        d->stats.bad_packets++;
        return 0;
    }

    size_t row_bytes = h.width / 2;
    if (len < VIC_HEADER_SIZE + row_bytes * h.lines || h.line + h.lines > VIC_MAX_HEIGHT) {
        d->stats.bad_packets++;
        return 0;
    }

    d->stats.packets++;
    if (d->have_seq && h.seq != d->next_seq)
        d->stats.lost_packets += (uint16_t)(h.seq - d->next_seq);
    d->have_seq = 1;
    d->next_seq = (uint16_t)(h.seq + 1);

    int completed = 0;

    /* A new frame number means the previous frame's last packet was lost. */
    if (d->work_frame != -1 && d->work_frame != h.frame) {
        publish(d);
        completed = 1;
    }
    if (d->work_frame != h.frame || h.width != d->width) {
        /* Keep stale rows from the previous frame rather than blanking, so a
         * dropped packet shows as a slightly old strip, not a black bar. */
        d->work_frame = h.frame;
        d->work_height = 0;
        d->width = h.width;
    }

    const uint8_t *src = buf + VIC_HEADER_SIZE;
    for (int y = 0; y < h.lines; y++) {
        uint8_t *dst = d->work + (size_t)(h.line + y) * h.width;
        for (size_t x = 0; x < row_bytes; x++) {
            uint8_t b = *src++;
            uint8_t lo = b & 0x0F, hi = b >> 4;
            dst[2 * x]     = d->swap_nibbles ? hi : lo;
            dst[2 * x + 1] = d->swap_nibbles ? lo : hi;
        }
    }
    if (h.line + h.lines > d->work_height)
        d->work_height = h.line + h.lines;

    if (h.last) {
        publish(d);
        d->work_frame = -1;
        completed = 1;
    }
    return completed;
}

void vic_to_argb(const vic_decoder *d, uint32_t *dst, int pitch)
{
    for (int y = 0; y < d->height; y++) {
        const uint8_t *s = d->pixels + (size_t)y * d->width;
        uint32_t *row = (uint32_t *)((uint8_t *)dst + (size_t)y * pitch);
        for (int x = 0; x < d->width; x++)
            row[x] = vic_palette[s[x] & 0x0F];
    }
}
