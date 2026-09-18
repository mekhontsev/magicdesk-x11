#pragma once
#include <stdint.h>

enum { LORIE_INSPECTION_LIMIT = 256 };
enum LorieInspectionFlags {
    LORIE_INSPECT_MAPPED = 1, LORIE_INSPECT_REALIZED = 2,
    LORIE_INSPECT_INPUT_ONLY = 4, LORIE_INSPECT_OVERRIDE_REDIRECT = 8,
    LORIE_INSPECT_MODAL = 16
};
enum LorieInspectionType {
    LORIE_INSPECT_UNKNOWN, LORIE_INSPECT_NORMAL, LORIE_INSPECT_DIALOG,
    LORIE_INSPECT_MENU, LORIE_INSPECT_DROPDOWN, LORIE_INSPECT_POPUP,
    LORIE_INSPECT_TOOLTIP, LORIE_INSPECT_SPLASH, LORIE_INSPECT_UTILITY,
    LORIE_INSPECT_OTHER
};
typedef struct {
    uint32_t id, parent, transientFor, leader;
    int32_t x, y, width, height;
    uint32_t flags, type;
    char title[192];
} LorieInspectionNode;
typedef struct {
    uint32_t window, focus, focusKind, count;
    int32_t screenWidth, screenHeight;
    uint8_t found, truncated;
} LorieInspectionResult;
