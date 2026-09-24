#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { LORIE_MAXIMIZED_HORIZONTAL = 1, LORIE_MAXIMIZED_VERTICAL = 2 };
typedef struct { uint32_t serial, requested, actual; } LorieMaximizedState;

static inline bool lorieMaximizedRequest(LorieMaximizedState* state, unsigned action, unsigned axes) {
    if (action > 2 || !axes || (axes & ~3u)) return false;
    state->requested = action == 0 ? state->requested & ~axes
            : action == 1 ? state->requested | axes : state->requested ^ axes;
    return true;
}

static inline bool lorieMaximizedConfirm(LorieMaximizedState* state, uint32_t serial, unsigned axes) {
    if (serial != state->serial || (axes & ~3u)) return false;
    state->requested = state->actual = axes;
    return true;
}
