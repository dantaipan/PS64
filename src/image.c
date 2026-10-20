#include "image.h"

#include <stdlib.h>

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

int image_decode(const void *data, size_t len, uint32_t **argb, int *w, int *h)
{
    if (!data || len == 0 || len > 64u * 1024 * 1024)
        return -1;
    int n;
    unsigned char *rgba = stbi_load_from_memory(data, (int)len, w, h, &n, 4);
    if (!rgba)
        return -1;
    size_t count = (size_t)*w * (size_t)*h;
    uint32_t *out = malloc(count * sizeof(*out));
    if (!out) {
        stbi_image_free(rgba);
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        const unsigned char *p = rgba + i * 4;
        out[i] = (uint32_t)p[3] << 24 | (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    }
    stbi_image_free(rgba);
    *argb = out;
    return 0;
}
