#pragma once

#include "embedded.h"

// Private connection wire format. Public callers use the semantic operations in embedded.h.
enum LorieOutputOperation {
    LORIE_OUTPUT_BIND = 0, LORIE_OUTPUT_RESIZE = 1, LORIE_OUTPUT_POINTER = 2, LORIE_OUTPUT_KEY = 3,
    LORIE_OUTPUT_RELEASE = 4, LORIE_OUTPUT_FOCUS = 5, LORIE_OUTPUT_TEXT = 6,
    LORIE_OUTPUT_OBSERVE = 7, LORIE_OUTPUT_CLOSE = 8, LORIE_OUTPUT_DPI = 9,
    LORIE_OUTPUT_FULLSCREEN_CONFIRM = 10
};

typedef struct {
    uint8_t t, operation, down;
    uint32_t output, window;
    int32_t x, y;
    uint16_t detail;
} LorieOutputCommand;

void lorieSendOutputCommand(LorieConnection*, const LorieOutputCommand*);
