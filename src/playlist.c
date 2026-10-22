#include "playlist.h"

static unsigned next_random(unsigned *s)
{
    *s = *s * 1103515245u + 12345u;
    return *s >> 8;
}

/* The order for p->mode, with entry first at position 0. */
static void build(playlist *p, int first)
{
    for (int i = 0; i < p->count; i++)
        p->order[i] = i;
    p->pos = first;
    if (p->mode != PLAY_SHUFFLE || p->count < 2)
        return;
    unsigned s = p->seed;
    for (int i = p->count - 1; i > 0; i--) {             /* Fisher-Yates */
        int j = (int)(next_random(&s) % (unsigned)(i + 1));
        int t = p->order[i];
        p->order[i] = p->order[j];
        p->order[j] = t;
    }
    for (int i = 0; i < p->count; i++)                   /* the current tune plays first */
        if (p->order[i] == first) {
            p->order[i] = p->order[0];
            p->order[0] = first;
            break;
        }
    p->pos = 0;
}

void playlist_start(playlist *p, int count, int start, int mode, unsigned seed)
{
    p->count = count < 0 ? 0 : count > PLAYLIST_MAX ? PLAYLIST_MAX : count;
    p->mode = mode;
    p->seed = seed;
    if (start < 0 || start >= p->count)
        start = 0;
    build(p, start);
}

int playlist_current(const playlist *p)
{
    return p->count ? p->order[p->pos] : -1;
}

int playlist_next(playlist *p, int by_hand, int wrap)
{
    if (!p->count)
        return -1;
    if (p->mode == PLAY_REPEAT_ONE && !by_hand)
        return playlist_current(p);
    if (p->pos + 1 >= p->count) {
        if (!wrap)
            return -1;
        p->pos = 0;
    } else {
        p->pos++;
    }
    return playlist_current(p);
}

int playlist_prev(playlist *p)
{
    if (!p->count)
        return -1;
    p->pos = p->pos > 0 ? p->pos - 1 : p->count - 1;
    return playlist_current(p);
}

void playlist_set_mode(playlist *p, int mode)
{
    int cur = playlist_current(p);
    p->mode = mode;
    if (cur >= 0) {
        p->seed = p->seed * 31u + 7u;
        build(p, cur);
    }
}
