#pragma once

typedef enum {
    LORIE_WINDOW_APPLICATION,
    LORIE_WINDOW_SPLASH,
    LORIE_WINDOW_UNCLASSIFIED,
    LORIE_WINDOW_DIALOG
} LorieWindowRole;

static inline LorieWindowRole lorieWindowRole(int splash, int dialog, int typed, int wmClass, int wmState) {
    if (splash) return LORIE_WINDOW_SPLASH;
    if (dialog) return LORIE_WINDOW_DIALOG;
    return typed || wmClass || wmState ? LORIE_WINDOW_APPLICATION : LORIE_WINDOW_UNCLASSIFIED;
}
