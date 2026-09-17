#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

static inline bool lorieBufferLayout(int32_t width, int32_t stride, int32_t height,
                                     uint64_t offset, size_t* bytes) {
    if (width <= 0 || height <= 0 || stride < width || stride > INT32_MAX / 4 || offset % 4)
        return false;
    /* Importers need pixels in the last row, not its trailing padding. */
    uint64_t size = ((uint64_t)(height - 1) * stride + width) * 4;
    if (size > SIZE_MAX || offset > INT64_MAX || size > INT64_MAX - offset)
        return false;
    *bytes = (size_t)size;
    return true;
}

static inline bool loriePresentSizeMatches(int screenWidth, int screenHeight, int width, int height) {
    return screenWidth > 0 && screenHeight > 0 && screenWidth == width && screenHeight == height;
}
