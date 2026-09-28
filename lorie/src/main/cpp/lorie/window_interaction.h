#pragma once
#include <stdint.h>

enum { LORIE_INTERACTION_NONE, LORIE_INTERACTION_ACTIVATE, LORIE_INTERACTION_MINIMIZE };
enum { LORIE_STATE_ACTIVE = 1, LORIE_STATE_MINIMIZED = 2, LORIE_STATE_ATTENTION = 4 };
typedef struct {
    uint32_t serial;
    uint8_t action, attention, actual;
} LorieWindowInteraction;

// X timestamps wrap. A request needs a real, recent host-delivered input event;
// source=2 (pager) alone is not authorization to steal Android focus.
static inline int lorieActivationTimestamp(uint32_t requested, uint32_t input, uint32_t now) {
    return requested && input && (int32_t)(requested - input) >= 0
            && (int32_t)(now - requested) >= 0 && (uint32_t)(now - input) <= 10000;
}
