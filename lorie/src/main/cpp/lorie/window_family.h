#pragma once

#include <windowstr.h>

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
