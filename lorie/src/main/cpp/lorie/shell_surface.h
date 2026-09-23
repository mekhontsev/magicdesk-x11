#pragma once
#include <stdint.h>

#define LORIE_SHELL_INPUT_LIMIT 128
typedef struct { int32_t left, top, right, bottom; } LorieShellRect;
typedef struct {
    uint32_t role, mapped, inputComplete, inputCount;
    int32_t x, y, width, height;
    LorieShellRect paint;
    uint32_t strut[12];
    LorieShellRect input[LORIE_SHELL_INPUT_LIMIT];
} LorieShellInfo;

// Native X properties/geometry, never Android display or task identities.
enum { LORIE_SHELL_DESKTOP = 1, LORIE_SHELL_DOCK = 2 };
