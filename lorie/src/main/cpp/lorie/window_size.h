#pragma once
#include <stddef.h>
#include <stdint.h>

enum {
    LORIE_WINDOW_SIZE_LIMIT = 16384,
    LORIE_HINT_MIN_SIZE = 1u << 4,
    LORIE_HINT_MAX_SIZE = 1u << 5
};

static inline int lorieWindowSizeAxis(int requested, const uint32_t* hints, size_t count, int axis) {
    int minimum = 1, maximum = LORIE_WINDOW_SIZE_LIMIT;
    // WM_NORMAL_HINTS is a CARD32 wire array, not Xlib's host-sized XSizeHints.
    if (hints && count >= 7 && (hints[0] & LORIE_HINT_MIN_SIZE)) {
        uint32_t value = hints[5 + axis];
        if (value > 0 && value <= LORIE_WINDOW_SIZE_LIMIT) minimum = (int)value;
    }
    if (hints && count >= 9 && (hints[0] & LORIE_HINT_MAX_SIZE)) {
        uint32_t value = hints[7 + axis];
        if (value > 0 && value <= INT32_MAX)
            maximum = value < LORIE_WINDOW_SIZE_LIMIT ? (int)value : LORIE_WINDOW_SIZE_LIMIT;
    }
    // Contradictory client hints must not create a resize feedback loop.
    if (maximum < minimum) { minimum = 1; maximum = LORIE_WINDOW_SIZE_LIMIT; }
    return requested < minimum ? minimum : requested > maximum ? maximum : requested;
}
