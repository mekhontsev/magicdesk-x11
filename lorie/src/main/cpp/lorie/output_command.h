#pragma once

#include "embedded.h"

// Private connection wire format. Public callers use the semantic operations in embedded.h.
enum LorieOutputOperation {
    LORIE_OUTPUT_BIND = 0, LORIE_OUTPUT_RESIZE = 1, LORIE_OUTPUT_POINTER = 2, LORIE_OUTPUT_KEY = 3,
    LORIE_OUTPUT_RELEASE = 4, LORIE_OUTPUT_FOCUS = 5, LORIE_OUTPUT_TEXT = 6,
    LORIE_OUTPUT_OBSERVE = 7, LORIE_OUTPUT_CLOSE = 8, LORIE_OUTPUT_DPI = 9,
    LORIE_OUTPUT_FULLSCREEN_CONFIRM = 10, LORIE_OUTPUT_INSPECT = 11,
    LORIE_OUTPUT_SHELL = 12, LORIE_OUTPUT_VIEWPORT = 13, LORIE_OUTPUT_BLUR = 14,
    LORIE_OUTPUT_MAXIMIZED_CONFIRM = 15, LORIE_OUTPUT_SCROLL = 16,
    LORIE_OUTPUT_TOUCH = 17, LORIE_OUTPUT_TABLET = 18, LORIE_OUTPUT_CANCEL_CONTACTS = 19,
    LORIE_OUTPUT_INTERACTION_CONFIRM = 20, LORIE_OUTPUT_COLOR_SCHEME = 21
};

typedef struct {
    uint8_t t, operation, down;
    uint32_t output, window;
    int32_t x, y;
    uint16_t detail;
    uint32_t serial;
    LorieShellRect viewport;
    float horizontal, vertical;
    float pressure, tiltX, tiltY;
    uint8_t phase, buttons, eraser, proximity;
} LorieOutputCommand;

void lorieSendOutputCommand(LorieConnection*, const LorieOutputCommand*);
