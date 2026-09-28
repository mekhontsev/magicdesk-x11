#pragma once
#include <stddef.h>
#include <stdint.h>

enum {
    LORIE_WINDOW_SIZE_LIMIT = 16384,
    LORIE_HINT_MIN_SIZE = 1u << 4,
    LORIE_HINT_MAX_SIZE = 1u << 5,
    LORIE_HINT_RESIZE_INC = 1u << 6,
    LORIE_HINT_ASPECT = 1u << 7,
    LORIE_HINT_BASE_SIZE = 1u << 8
};

typedef struct {
    int baseWidth, baseHeight, widthIncrement, heightIncrement;
    int minAspectX, minAspectY, maxAspectX, maxAspectY;
    int aspectBaseWidth, aspectBaseHeight;
} LorieResizeRules;
typedef struct {
    int minWidth, minHeight, maxWidth, maxHeight;
    LorieResizeRules resize;
} LorieWindowConstraints;

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

static inline LorieWindowConstraints lorieWindowSizeHints(const uint32_t* hints, size_t count) {
    LorieWindowConstraints result = {
        .minWidth = lorieWindowSizeAxis(1, hints, count, 0),
        .minHeight = lorieWindowSizeAxis(1, hints, count, 1),
        .maxWidth = lorieWindowSizeAxis(INT32_MAX, hints, count, 0),
        .maxHeight = lorieWindowSizeAxis(INT32_MAX, hints, count, 1)
    };
    if (!hints || !count) return result;
    int base[2] = {0}, increment[2] = {1, 1};
    int* minimum[2] = {&result.minWidth, &result.minHeight};
    int maximum[2] = {result.maxWidth, result.maxHeight};
    int* aspectBase[2] = {&result.resize.aspectBaseWidth, &result.resize.aspectBaseHeight};
    for (int axis = 0; axis < 2; axis++) {
        if (count >= 17 && (hints[0] & LORIE_HINT_BASE_SIZE) && hints[15 + axis] <= LORIE_WINDOW_SIZE_LIMIT) {
            base[axis] = *aspectBase[axis] = (int)hints[15 + axis];
            if (!(hints[0] & LORIE_HINT_MIN_SIZE) && base[axis] > 0 && base[axis] <= maximum[axis])
                *minimum[axis] = base[axis];
        } else if (count >= 7 && (hints[0] & LORIE_HINT_MIN_SIZE)) base[axis] = *minimum[axis];
        if (count >= 11 && (hints[0] & LORIE_HINT_RESIZE_INC)
                && hints[9 + axis] > 0 && hints[9 + axis] <= LORIE_WINDOW_SIZE_LIMIT)
            increment[axis] = (int)hints[9 + axis];
    }
    result.resize.baseWidth = base[0]; result.resize.baseHeight = base[1];
    result.resize.widthIncrement = increment[0]; result.resize.heightIncrement = increment[1];
    if (count >= 15 && (hints[0] & LORIE_HINT_ASPECT)
            && hints[11] > 0 && hints[11] <= INT32_MAX && hints[12] > 0 && hints[12] <= INT32_MAX
            && hints[13] > 0 && hints[13] <= INT32_MAX && hints[14] > 0 && hints[14] <= INT32_MAX
            && (uint64_t)hints[11] * hints[14] <= (uint64_t)hints[13] * hints[12]) {
        result.resize.minAspectX = hints[11]; result.resize.minAspectY = hints[12];
        result.resize.maxAspectX = hints[13]; result.resize.maxAspectY = hints[14];
    }
    return result;
}
