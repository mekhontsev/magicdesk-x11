#pragma once

#include <stddef.h>
#include <libxcvt/libxcvt.h>

static inline struct libxcvt_mode_info* lorieVirtualMode(int width, int height, int framerate) {
    if (width < 1 || height < 1 || width > 32767 || height > 32767) return NULL;
    // CVT rounds physical scanout widths down to 8 pixels (and special-cases
    // 1366x768). Use its timings, but keep our virtual canvas pixel-exact.
    struct libxcvt_mode_info* mode = libxcvt_gen_mode_info((width + 7) & ~7, height, framerate, false, false);
    if (mode) {
        mode->hdisplay = width;
        mode->vdisplay = height;
    }
    return mode;
}
