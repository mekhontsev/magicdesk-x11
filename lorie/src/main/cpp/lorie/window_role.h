#pragma once

typedef enum {
    LORIE_WINDOW_APPLICATION,
    LORIE_WINDOW_SPLASH,
    LORIE_WINDOW_UNCLASSIFIED
} LorieWindowRole;

static inline LorieWindowRole lorieWindowRole(int splash, int typed, int wmClass, int wmState) {
    if (splash) return LORIE_WINDOW_SPLASH;
    return typed || wmClass || wmState ? LORIE_WINDOW_APPLICATION : LORIE_WINDOW_UNCLASSIFIED;
}
