#include "audiostream.h"

#include <string.h>

void audio_init(audio_decoder *d)
{
    memset(d, 0, sizeof(*d));
}

size_t audio_feed(audio_decoder *d, const uint8_t *buf, size_t len,
                  int16_t *out, size_t max_frames)
{
    size_t frames = len >= AUDIO_HEADER_SIZE ? (len - AUDIO_HEADER_SIZE) / 4 : 0;
    if (frames == 0 || (len - AUDIO_HEADER_SIZE) % 4 != 0 || frames > max_frames) {
        d->stats.bad_packets++;
        return 0;
    }

    uint16_t seq = (uint16_t)(buf[0] | (buf[1] << 8));
    d->stats.packets++;
    if (d->have_seq && seq != d->next_seq)
        d->stats.lost_packets += (uint16_t)(seq - d->next_seq);
    d->have_seq = 1;
    d->next_seq = (uint16_t)(seq + 1);

    const uint8_t *p = buf + AUDIO_HEADER_SIZE;
    for (size_t i = 0; i < frames * 2; i++, p += 2)
        out[i] = (int16_t)(uint16_t)(p[0] | (p[1] << 8));
    return frames;
}
