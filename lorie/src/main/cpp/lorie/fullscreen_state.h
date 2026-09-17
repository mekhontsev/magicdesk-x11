#pragma once
#include <stdbool.h>
#include <stdint.h>

// Toggles compose against the latest request, not an outstanding host reply.
typedef struct {
    uint32_t serial;
    bool requested, actual;
} LorieFullscreenState;

static inline bool lorieFullscreenRequest(LorieFullscreenState* state, uint32_t action) {
    if (action > 2) return false;
    state->requested = action == 2 ? !state->requested : action == 1;
    if (++state->serial == 0) ++state->serial;
    return true;
}

static inline bool lorieFullscreenConfirm(LorieFullscreenState* state, uint32_t serial, bool actual) {
    if (serial != state->serial) return false;
    state->requested = state->actual = actual;
    return true;
}
