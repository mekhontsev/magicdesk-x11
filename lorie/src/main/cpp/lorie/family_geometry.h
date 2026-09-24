#pragma once
#include "shell_surface.h"

/* Owner-relative geometry for one borrowed dependent output, not a shell role. */
typedef struct {
    int32_t width, height;
    uint32_t inputComplete, inputCount;
    LorieShellRect paint;
    LorieShellRect input[LORIE_SHELL_INPUT_LIMIT];
} LorieFamilyGeometry;
