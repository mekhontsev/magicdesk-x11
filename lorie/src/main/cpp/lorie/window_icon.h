#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define LORIE_WINDOW_ICON_SIZE 64
#define LORIE_WINDOW_ICON_PIXELS (LORIE_WINDOW_ICON_SIZE * LORIE_WINDOW_ICON_SIZE)

// EWMH CARDINAL data is unpremultiplied ARGB. Validate every length before
// selecting and fitting one image into a bounded, transparent icon square.
static inline int lorieWindowIcon(const uint32_t* data, size_t count, uint32_t* result) {
    memset(result, 0, LORIE_WINDOW_ICON_PIXELS * sizeof(*result));
    if (!data || count > 1024 * 1024) return 0;
    const uint32_t* best = NULL;
    uint32_t bw = 0, bh = 0, bestSide = 0;
    for (size_t offset = 0; offset < count;) {
        if (count - offset < 2) return 0;
        uint32_t width = data[offset++], height = data[offset++];
        if (!width || !height || width > (count - offset) / height) return 0;
        uint32_t side = width > height ? width : height;
        if (!best || (bestSide < LORIE_WINDOW_ICON_SIZE && side > bestSide)
                || (side >= LORIE_WINDOW_ICON_SIZE && side < bestSide)) {
            best = data + offset; bw = width; bh = height; bestSide = side;
        }
        offset += (size_t)width * height;
    }
    if (!best) return 0;
    uint32_t width = (uint64_t)bw * LORIE_WINDOW_ICON_SIZE / bestSide;
    uint32_t height = (uint64_t)bh * LORIE_WINDOW_ICON_SIZE / bestSide;
    if (!width) width = 1;
    if (!height) height = 1;
    uint32_t left = (LORIE_WINDOW_ICON_SIZE - width) / 2;
    uint32_t top = (LORIE_WINDOW_ICON_SIZE - height) / 2;
    for (uint32_t y = 0; y < height; y++) for (uint32_t x = 0; x < width; x++)
        result[(top + y) * LORIE_WINDOW_ICON_SIZE + left + x] =
                best[(size_t)(y * bh / height) * bw + x * bw / width];
    return 1;
}
