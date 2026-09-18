#pragma once

#include <windowstr.h>

/* Collect before restacking: raising a member changes the sibling links used
 * by the family walk. The owner is first, followed by transients back to front. */
typedef struct {
    WindowPtr members[256];
    WindowPtr focus;
    unsigned count;
    Bool truncated;
} LorieWindowActivation;

static inline void lorieWindowActivationAdd(LorieWindowActivation* activation,
        WindowPtr window, Bool modal) {
    if (window == activation->members[0]) return;
    if (activation->count == 256) { activation->truncated = TRUE; return; }
    activation->members[activation->count++] = window;
    if (modal) activation->focus = window;
}

static inline WindowPtr lorieWindowActivationApply(const LorieWindowActivation* activation,
        WindowPtr root, void (*raise)(WindowPtr)) {
    if (activation->truncated) return NULL;
    for (unsigned i = 0; i < activation->count; i++) {
        WindowPtr stack = activation->members[i];
        while (stack->parent && stack->parent != root) stack = stack->parent;
        raise(stack);
    }
    return activation->focus;
}

/* Toolkit focus windows are real children, not WM_TRANSIENT_FOR windows.
 * Walk actual ancestry first, then its nearest transient link. Bound both
 * kinds of links together: client-supplied transient chains can contain cycles. */
static inline Bool lorieWindowFamilyContains(WindowPtr window, WindowPtr owner, WindowPtr root,
        WindowPtr (*transientFor)(WindowPtr)) {
    unsigned remaining = 64;
    while (window && window != root) {
        WindowPtr transient = NULL;
        for (WindowPtr ancestor = window; ancestor && ancestor != root; ancestor = ancestor->parent) {
            if (!remaining) return FALSE;
            remaining--;
            if (ancestor == owner) return TRUE;
            if (!transient) transient = transientFor(ancestor);
        }
        window = transient;
    }
    return FALSE;
}
