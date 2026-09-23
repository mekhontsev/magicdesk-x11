#include <dix-config.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <propertyst.h>
#include <dix.h>
#include "lorie.h"
#include "window_model.h"

extern ScreenPtr pScreenPtr;
typedef struct ShellRecord {
    struct ShellRecord* next;
    XID window;
    Bool seen;
    LorieShellInfo info;
} ShellRecord;
static ShellRecord* records;
static uint32_t owner;
static Bool dirty;

static PropertyPtr property(WindowPtr window, const char* name) {
    PropertyPtr value = NULL;
    Atom atom = MakeAtom(name, strlen(name), TRUE);
    return dixLookupProperty(&value, window, atom, serverClient, DixReadAccess) == Success ? value : NULL;
}
static unsigned role(WindowPtr window) {
    PropertyPtr type = property(window, "_NET_WM_WINDOW_TYPE");
    if (window->drawable.class != InputOutput || window->overrideRedirect || !type || type->format != 32 || type->type != XA_ATOM)
        return 0;
    Atom dock = MakeAtom("_NET_WM_WINDOW_TYPE_DOCK", 24, TRUE);
    Atom desktop = MakeAtom("_NET_WM_WINDOW_TYPE_DESKTOP", 27, TRUE);
    for (unsigned long i = 0; i < type->size; i++) {
        if (((CARD32*)type->data)[i] == dock) return LORIE_SHELL_DOCK;
        if (((CARD32*)type->data)[i] == desktop) return LORIE_SHELL_DESKTOP;
    }
    return 0;
}
static void state(uint32_t id, Bool available) {
    lorieEvent event = {.shell = {.t = EVENT_SHELL_STATE, .owner = id, .available = available}};
    lorieSendOutputFrame(&event);
}
void lorieShellReset(void) {
    while (records) { ShellRecord* next = records->next; free(records); records = next; }
    owner = 0; dirty = FALSE;
}
void lorieShellGeometryChanged(void) { if (owner) dirty = TRUE; }
Bool lorieShellWindow(XID window) {
    if (!owner || lorieWindowManagerExternal()) return FALSE;
    for (ShellRecord* r = records; r; r = r->next) if (r->window == window) return TRUE;
    return FALSE;
}
void lorieShellConfigure(uint32_t id, int width, int height) {
    if (!id) return;
    if (owner && owner != id) { state(id, FALSE); return; }
    if (width == 0 && height == 0) { lorieShellReset(); state(id, FALSE); return; }
    if (width < 1 || height < 1 || width > 16384 || height > 16384 || lorieWindowManagerExternal()) {
        if (owner == id) lorieShellReset();
        state(id, FALSE); return;
    }
    owner = id;
    dirty = TRUE;
    lorieConfigureNotify(width, height, 60, 0, NULL);
    state(id, TRUE);
}
typedef struct { WindowPtr owner; LorieShellInfo* info; RegionRec input; unsigned count; } Family;
static void family(WindowPtr window, void* data) {
    Family* f = data;
    if (++f->count > LORIE_MAX_FAMILY_LAYERS) { f->info->inputComplete = FALSE; return; }
    int x = window->drawable.x - f->owner->drawable.x, y = window->drawable.y - f->owner->drawable.y;
    f->info->paint.left = min(f->info->paint.left, x);
    f->info->paint.top = min(f->info->paint.top, y);
    f->info->paint.right = max(f->info->paint.right, x + window->drawable.width);
    f->info->paint.bottom = max(f->info->paint.bottom, y + window->drawable.height);
    RegionRec region;
    RegionNull(&region);
    RegionCopy(&region, &window->winSize);
    RegionTranslate(&region, -window->drawable.x, -window->drawable.y);
    if (wInputShape(window)) RegionIntersect(&region, &region, wInputShape(window));
    RegionTranslate(&region, x, y);
    RegionUnion(&f->input, &f->input, &region);
    RegionUninit(&region);
}
static void publish(WindowPtr window) {
    unsigned kind = role(window);
    if (!kind) return;
    ShellRecord* record = records;
    unsigned count = 0;
    for (ShellRecord* r = records; r; r = r->next) count++;
    while (record && record->window != window->drawable.id) record = record->next;
    if (!record && (!window->realized || count >= 32)) return;
    Bool fresh = !record;
    if (!record) {
        record = calloc(1, sizeof(*record));
        if (!record) return;
        record->window = window->drawable.id; record->next = records; records = record;
    }
    record->seen = TRUE;
    LorieShellInfo info = {.role = kind, .mapped = window->realized, .inputComplete = TRUE,
            .x = window->drawable.x, .y = window->drawable.y,
            .width = window->drawable.width, .height = window->drawable.height,
            .paint = {0, 0, window->drawable.width, window->drawable.height}};
    PropertyPtr partial = property(window, "_NET_WM_STRUT_PARTIAL");
    if (partial && partial->type == XA_CARDINAL && partial->format == 32 && partial->size == 12)
        memcpy(info.strut, partial->data, sizeof(info.strut));
    else {
        PropertyPtr strut = property(window, "_NET_WM_STRUT");
        if (strut && strut->type == XA_CARDINAL && strut->format == 32 && strut->size == 4) {
            memcpy(info.strut, strut->data, 4 * sizeof(uint32_t));
            info.strut[5] = info.strut[7] = pScreenPtr->height - 1;
            info.strut[9] = info.strut[11] = pScreenPtr->width - 1;
        }
    }
    if (window->realized) {
        Family f = {.owner = window, .info = &info};
        RegionNull(&f.input);
        lorieWindowFamily(window, family, &f);
        if (RegionNumRects(&f.input) > LORIE_SHELL_INPUT_LIMIT) info.inputComplete = FALSE;
        if (info.inputComplete) {
            info.inputCount = RegionNumRects(&f.input);
            BoxPtr rects = RegionRects(&f.input);
            for (unsigned i = 0; i < info.inputCount; i++)
                info.input[i] = (LorieShellRect){rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2};
        }
        RegionUninit(&f.input);
    }
    if (fresh || memcmp(&record->info, &info, sizeof(info))) {
        record->info = info;
        lorieEvent event = {.shell = {.t = EVENT_OUTPUT_SHELL, .owner = owner, .window = record->window}};
        lorieSendShellInfo(&event, &info);
    }
}
static void walk(WindowPtr parent) {
    for (WindowPtr child = parent->lastChild; child; child = child->prevSib) { publish(child); walk(child); }
}
void lorieShellRefresh(void) {
    if (!owner || !dirty) return;
    dirty = FALSE;
    if (lorieWindowManagerExternal()) { uint32_t id = owner; lorieShellReset(); state(id, FALSE); return; }
    for (ShellRecord* r = records; r; r = r->next) r->seen = FALSE;
    walk(pScreenPtr->root);
    ShellRecord** link = &records;
    while (*link) {
        ShellRecord* r = *link;
        if (r->seen) { link = &r->next; continue; }
        lorieEvent event = {.shell = {.t = EVENT_OUTPUT_SHELL, .owner = owner, .window = r->window, .removed = TRUE}};
        lorieSendShellInfo(&event, NULL);
        *link = r->next; free(r);
    }
    lorieEvent done = {.type = EVENT_OUTPUT_WINDOWS_DONE};
    lorieSendOutputFrame(&done);
}
