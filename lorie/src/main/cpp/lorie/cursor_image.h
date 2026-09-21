#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LORIE_CURSOR_SIZE_LIMIT 512
enum { LORIE_CURSOR_DEFAULT, LORIE_CURSOR_HIDDEN, LORIE_CURSOR_IMAGE };

typedef struct {
    uint32_t kind, width, height, hotspotX, hotspotY;
} LorieCursorInfo;

static inline bool lorieCursorValid(const LorieCursorInfo* info) {
    if (info->kind == LORIE_CURSOR_DEFAULT || info->kind == LORIE_CURSOR_HIDDEN)
        return !(info->width || info->height || info->hotspotX || info->hotspotY);
    return info->kind == LORIE_CURSOR_IMAGE && info->width && info->height &&
            info->width <= LORIE_CURSOR_SIZE_LIMIT && info->height <= LORIE_CURSOR_SIZE_LIMIT &&
            info->hotspotX < info->width && info->hotspotY < info->height;
}

static inline size_t lorieCursorPixelCount(const LorieCursorInfo* info) {
    return lorieCursorValid(info) ? (size_t)info->width * info->height : 0;
}

// XRender supplies premultiplied ARGB; the embedding contract uses straight ARGB.
static inline uint32_t lorieCursorStraightArgb(uint32_t pixel) {
    unsigned alpha = pixel >> 24;
    if (!alpha) return 0;
    if (alpha == 255) return pixel;
    uint32_t result = alpha << 24;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        unsigned color = (((pixel >> shift) & 255) * 255 + alpha / 2) / alpha;
        result |= (color > 255 ? 255 : color) << shift;
    }
    return result;
}

static inline uint32_t lorieCursorMonoPixel(const uint8_t* source, const uint8_t* mask,
        size_t offset, unsigned bit, uint32_t foreground, uint32_t background) {
    return (mask[offset] & bit) ? 0xff000000 | ((source[offset] & bit) ? foreground : background) : 0;
}
