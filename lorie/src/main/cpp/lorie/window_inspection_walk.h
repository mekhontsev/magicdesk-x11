#pragma once
#include <windowstr.h>

/* Walk real children too, including hidden input windows. Family policy is
 * borrowed from the compositor; a popup's actual descendants inherit it. */
static inline Bool lorieInspectMember(WindowPtr window, WindowPtr owner, WindowPtr root,
        Bool (*member)(WindowPtr, WindowPtr)) {
    for (unsigned depth = 0; window && window != root && depth < 64; depth++, window = window->parent)
        if (member(window, owner)) return TRUE;
    return FALSE;
}

typedef struct { unsigned count; Bool truncated; } LorieInspectionWalk;
static inline LorieInspectionWalk lorieInspectFamily(WindowPtr root, WindowPtr owner, unsigned limit,
        Bool (*member)(WindowPtr, WindowPtr), void (*emit)(WindowPtr, void*), void* context) {
    LorieInspectionWalk result = {0};
    if (!root || !owner || !limit) return result;
    emit(owner, context);
    result.count = 1;
    unsigned scanned = 0;
    WindowPtr window = root->lastChild;
    while (window) {
        if (++scanned > 8192) { result.truncated = TRUE; break; }
        if (window != owner && lorieInspectMember(window, owner, root, member)) {
            if (result.count == limit) { result.truncated = TRUE; break; }
            emit(window, context);
            result.count++;
        }
        if (window->lastChild) { window = window->lastChild; continue; }
        while (window != root && !window->prevSib) window = window->parent;
        if (window == root) break;
        window = window->prevSib;
    }
    return result;
}
