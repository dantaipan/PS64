#include "image.h"

#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_GIF
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

int image_decode(const void *data, size_t len, uint32_t **argb, int *w, int *h,
                 int *frames, int **delays_ms)
{
    if (!data || len == 0 || len > 64u * 1024 * 1024)
        return -1;
    int n, z = 1, *gif_delays = NULL;
    unsigned char *rgba;
    if (len >= 3 && !memcmp(data, "GIF", 3))
        rgba = stbi_load_gif_from_memory(data, (int)len, &gif_delays, w, h, &z, &n, 4);
    else
        rgba = stbi_load_from_memory(data, (int)len, w, h, &n, 4);
    if (!rgba) {
        free(gif_delays);
        return -1;
    }
    if (z < 1)
        z = 1;
    if (z > IMAGE_MAX_FRAMES)
        z = IMAGE_MAX_FRAMES;
    size_t count = (size_t)*w * (size_t)*h * (size_t)z;
    uint32_t *out = malloc(count * sizeof(*out));
    int *delays = malloc((size_t)z * sizeof(*delays));
    if (!out || !delays) {
        free(out);
        free(delays);
        free(gif_delays);
        stbi_image_free(rgba);
        return -1;
    }
    for (int i = 0; i < z; i++) {
        int d = gif_delays ? gif_delays[i] : 0;
        delays[i] = d <= 0 ? 1500 : d < 100 ? 100 : d;
    }
    free(gif_delays);
    for (size_t i = 0; i < count; i++) {
        const unsigned char *p = rgba + i * 4;
        out[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    }
    stbi_image_free(rgba);
    *argb = out;
    *frames = z;
    *delays_ms = delays;
    return 0;
}
