#include <dix-config.h>
#include "lorie.h"
#include "window_model.h"

typedef struct {
    WindowPtr owner;
    LorieFamilyGeometry* geometry;
    RegionRec input;
    unsigned count;
    Bool childrenOnly, painted;
} FamilyGeometry;

static void collect(WindowPtr window, void* data) {
    FamilyGeometry* family = data;
    if (family->childrenOnly && window == family->owner) return;
    LorieFamilyGeometry* g = family->geometry;
    if (++family->count > LORIE_MAX_FAMILY_LAYERS) { g->inputComplete = FALSE; return; }
    int x = window->drawable.x - family->owner->drawable.x;
    int y = window->drawable.y - family->owner->drawable.y;
    LorieShellRect paint = {x, y, x + window->drawable.width, y + window->drawable.height};
    if (!family->painted) { g->paint = paint; family->painted = TRUE; }
    else {
        g->paint.left = min(g->paint.left, paint.left); g->paint.top = min(g->paint.top, paint.top);
        g->paint.right = max(g->paint.right, paint.right); g->paint.bottom = max(g->paint.bottom, paint.bottom);
    }
    RegionRec region;
    RegionNull(&region);
    RegionCopy(&region, &window->winSize);
    RegionTranslate(&region, -window->drawable.x, -window->drawable.y);
    if (wInputShape(window)) RegionIntersect(&region, &region, wInputShape(window));
    RegionTranslate(&region, x, y);
    RegionUnion(&family->input, &family->input, &region);
    RegionUninit(&region);
}

void lorieWindowFamilyGeometry(WindowPtr owner, Bool childrenOnly, LorieFamilyGeometry* geometry) {
    *geometry = (LorieFamilyGeometry){.width = owner->drawable.width, .height = owner->drawable.height,
            .inputComplete = TRUE};
    FamilyGeometry family = {.owner = owner, .geometry = geometry, .childrenOnly = childrenOnly};
    RegionNull(&family.input);
    if (owner->realized) lorieWindowFamily(owner, collect, &family);
    if (RegionNumRects(&family.input) > LORIE_SHELL_INPUT_LIMIT) geometry->inputComplete = FALSE;
    if (geometry->inputComplete) {
        geometry->inputCount = RegionNumRects(&family.input);
        BoxPtr rects = RegionRects(&family.input);
        for (unsigned i = 0; i < geometry->inputCount; i++)
            geometry->input[i] = (LorieShellRect){rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2};
    }
    RegionUninit(&family.input);
}
