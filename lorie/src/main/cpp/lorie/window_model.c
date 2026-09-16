#include <dix-config.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <property.h>
#include <propertyst.h>
#include <inputstr.h>
#include <dix.h>
#include "lorie.h"
#include "window_model.h"
#include "window_icon.h"

extern ScreenPtr pScreenPtr;
extern DeviceIntPtr lorieKeyboard;

typedef struct WindowRecord {
    struct WindowRecord* next;
    XID id;
    Bool seen, mapped;
    char title[256];
    Bool hasIcon;
    uint32_t icon[LORIE_WINDOW_ICON_PIXELS];
} WindowRecord;

static WindowRecord* records;
static Bool dirty = TRUE, observing;
static RealizeWindowProcPtr realize;
static UnrealizeWindowProcPtr unrealize;
static PositionWindowProcPtr position;
static RestackWindowProcPtr restack;
static DestroyWindowProcPtr destroy;

static Atom atom(const char* name) { return MakeAtom(name, strlen(name), TRUE); }

static PropertyPtr property(WindowPtr window, const char* name) {
    PropertyPtr value = NULL;
    return dixLookupProperty(&value, window, atom(name), serverClient, DixReadAccess) == Success ? value : NULL;
}

static XID reference(WindowPtr window, const char* name) {
    PropertyPtr value = property(window, name);
    return value && value->type == XA_WINDOW && value->format == 32 && value->size == 1
            ? *(CARD32*)value->data : None;
}

static Bool hasAtom(WindowPtr window, const char* name, const char* item) {
    PropertyPtr value = property(window, name);
    if (!value || value->type != XA_ATOM || value->format != 32) return FALSE;
    Atom wanted = atom(item);
    for (unsigned long i = 0; i < value->size; i++) if (((CARD32*)value->data)[i] == wanted) return TRUE;
    return FALSE;
}

static WindowPtr lookup(XID id) {
    WindowPtr window = NULL;
    return id && dixLookupWindow(&window, id, serverClient, DixReadAccess) == Success ? window : NULL;
}

Bool lorieWindowBelongsTo(WindowPtr window, WindowPtr owner) {
    // Transient chains are client-controlled: reject cycles with a bounded walk.
    for (int depth = 0; window && depth < 64; depth++) {
        if (window == owner) return TRUE;
        XID parent = reference(window, "WM_TRANSIENT_FOR");
        if (!parent || parent == window->drawable.id || parent == pScreenPtr->root->drawable.id) return FALSE;
        window = lookup(parent);
    }
    return FALSE;
}

static Bool familyMember(WindowPtr window, WindowPtr owner) {
    if (lorieWindowBelongsTo(window, owner)) return TRUE;
    // Group transients and unparented popups follow the focused member, never all windows of a process.
    XID transient = reference(window, "WM_TRANSIENT_FOR");
    if (transient && transient != pScreenPtr->root->drawable.id) return FALSE;
    if (!window->overrideRedirect && !hasAtom(window, "_NET_WM_STATE", "_NET_WM_STATE_MODAL")) return FALSE;
    XID group = reference(window, "WM_CLIENT_LEADER");
    if (!group || group != reference(owner, "WM_CLIENT_LEADER")) return FALSE;
    WindowPtr focus = lorieKeyboard->focus ? lorieKeyboard->focus->win : NULL;
    return focus && focus != PointerRootWin && focus != NoneWin && lorieWindowBelongsTo(focus, owner);
}

static void visitFamily(WindowPtr node, WindowPtr owner, void (*visit)(WindowPtr, void*), void* data) {
    for (WindowPtr child = node->lastChild; child; child = child->prevSib) {
        if (!child->realized) continue;
        if (child->drawable.class == InputOutput && familyMember(child, owner)) visit(child, data);
        // Children are already in their parent's pixmap. Only separate client/transient surfaces need composition.
        if (child != owner && !familyMember(child, owner)) visitFamily(child, owner, visit, data);
    }
}

void lorieWindowFamily(WindowPtr owner, void (*visit)(WindowPtr, void*), void* data) {
    visitFamily(pScreenPtr->root, owner, visit, data);
}

static Bool isApplication(WindowPtr window) {
    if (window == pScreenPtr->root || window->drawable.class != InputOutput || window->overrideRedirect
            || reference(window, "WM_TRANSIENT_FOR")) return FALSE;
    if (hasAtom(window, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DESKTOP")
            || hasAtom(window, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_DOCK")
            || hasAtom(window, "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_SPLASH")) return FALSE;
    if (property(window, "WM_STATE")) return TRUE;
    return window->parent == pScreenPtr->root && (property(window, "WM_CLASS")
            || property(window, "WM_NAME") || property(window, "_NET_WM_NAME"));
}

static void publishWindow(WindowPtr window) {
    if (!isApplication(window)) return;
    WindowRecord* record = records;
    while (record && record->id != window->drawable.id) record = record->next;
    if (!record && !window->realized) return;
    Bool added = !record;
    if (!record) {
        record = calloc(1, sizeof(*record));
        if (!record) return;
        record->id = window->drawable.id;
        record->next = records;
        records = record;
    }
    record->seen = TRUE;
    char title[256] = {0};
    PropertyPtr value = property(window, "_NET_WM_NAME");
    if (!value || value->format != 8) value = property(window, "WM_NAME");
    if (value && value->format == 8) memcpy(title, value->data, min(value->size, sizeof(title) - 1));
    uint32_t icon[LORIE_WINDOW_ICON_PIXELS];
    value = property(window, "_NET_WM_ICON");
    Bool hasIcon = lorieWindowIcon(value && value->type == XA_CARDINAL && value->format == 32
            ? value->data : NULL, value ? value->size : 0, icon);
    if (added || record->mapped != window->realized || strcmp(record->title, title)
            || record->hasIcon != hasIcon || memcmp(record->icon, icon, sizeof(icon))) {
        record->mapped = window->realized;
        memcpy(record->title, title, sizeof(title));
        record->hasIcon = hasIcon;
        memcpy(record->icon, icon, sizeof(icon));
        lorieEvent event = {.windowInfo = {.t = EVENT_OUTPUT_WINDOW, .mapped = record->mapped,
                .window = record->id, .hasIcon = hasIcon}};
        memcpy(event.windowInfo.title, title, sizeof(title));
        lorieSendWindowInfo(&event, icon);
    }
}

static void walk(WindowPtr parent) {
    for (WindowPtr child = parent->lastChild; child; child = child->prevSib) {
        publishWindow(child);
        walk(child);
    }
}

void lorieWindowModelRefresh(void) {
    if (!observing || !dirty || !pScreenPtr || !pScreenPtr->root) return;
    dirty = FALSE;
    for (WindowRecord* record = records; record; record = record->next) record->seen = FALSE;
    walk(pScreenPtr->root);
    WindowRecord** link = &records;
    while (*link) {
        WindowRecord* record = *link;
        if (record->seen) { link = &record->next; continue; }
        lorieEvent event = {.windowInfo = {.t = EVENT_OUTPUT_WINDOW, .removed = TRUE, .window = record->id}};
        lorieSendOutputFrame(&event);
        *link = record->next;
        free(record);
    }
    lorieEvent committed = {.type = EVENT_OUTPUT_WINDOWS_DONE};
    lorieSendOutputFrame(&committed);
}

void lorieWindowModelReset(void) {
    while (records) { WindowRecord* record = records; records = record->next; free(record); }
    observing = FALSE;
    dirty = TRUE;
}

void lorieWindowModelObserve(void) { lorieWindowModelReset(); observing = TRUE; }

static void clientMessage(WindowPtr window, const char* protocol) {
    xEvent event = {0};
    event.u.u.type = ClientMessage;
    event.u.u.detail = 32;
    event.u.clientMessage.window = window->drawable.id;
    event.u.clientMessage.u.l.type = atom("WM_PROTOCOLS");
    event.u.clientMessage.u.l.longs0 = atom(protocol);
    event.u.clientMessage.u.l.longs1 = currentTime.milliseconds;
    TryClientEvents(wClient(window), NULL, &event, 1, NoEventMask, NoEventMask, NullGrab);
}

void lorieWindowClose(XID id) {
    WindowPtr window = lookup(id);
    if (!window || !isApplication(window)) return;
    if (hasAtom(window, "WM_PROTOCOLS", "WM_DELETE_WINDOW")) clientMessage(window, "WM_DELETE_WINDOW");
    else CloseDownClient(wClient(window));
}

void lorieWindowFocus(WindowPtr window) {
    if (!window || !window->realized) return;
    // Do not break toolkit popup grabs or replace a dialog's keyboard focus with its parent.
    if (lorieKeyboard->deviceGrab.grab) return;
    WindowPtr focus = lorieKeyboard->focus ? lorieKeyboard->focus->win : NULL;
    if (focus && focus != PointerRootWin && focus != NoneWin && lorieWindowBelongsTo(focus, window)) return;
    WindowPtr stack = window;
    while (stack->parent && stack->parent != pScreenPtr->root) stack = stack->parent;
    XID above = Above;
    ConfigureWindow(stack, CWStackMode, &above, serverClient);
    PropertyPtr hints = property(window, "WM_HINTS");
    Bool accepts = !hints || hints->format != 32 || hints->size < 2
            || !(((CARD32*)hints->data)[0] & 1) || ((CARD32*)hints->data)[1];
    if (accepts) SetInputFocus(serverClient, lorieKeyboard, window->drawable.id, RevertToParent, CurrentTime, FALSE);
    if (hasAtom(window, "WM_PROTOCOLS", "WM_TAKE_FOCUS")) clientMessage(window, "WM_TAKE_FOCUS");
}

static void propertyChanged(CallbackListPtr* list, void* closure, void* data) { dirty = TRUE; }
#define WINDOW_HOOK(name, member, saved) \
    static Bool name(WindowPtr w) { \
        dirty = TRUE; \
        ScreenPtr screen = w->drawable.pScreen; \
        screen->member = saved; \
        Bool result = screen->member(w); \
        saved = screen->member; \
        screen->member = name; \
        return result; \
    }
WINDOW_HOOK(onRealize, RealizeWindow, realize)
WINDOW_HOOK(onUnrealize, UnrealizeWindow, unrealize)
#undef WINDOW_HOOK
static Bool onDestroy(WindowPtr w) {
    dirty = TRUE;
    lorieOutputWindowDestroyed(w->drawable.id);
    ScreenPtr screen = w->drawable.pScreen;
    screen->DestroyWindow = destroy;
    Bool result = screen->DestroyWindow(w);
    destroy = screen->DestroyWindow;
    screen->DestroyWindow = onDestroy;
    return result;
}
static Bool onPosition(WindowPtr w, int x, int y) {
    dirty = TRUE;
    lorieOutputGeometryChanged();
    ScreenPtr screen = w->drawable.pScreen;
    screen->PositionWindow = position;
    Bool result = screen->PositionWindow(w, x, y);
    position = screen->PositionWindow;
    screen->PositionWindow = onPosition;
    return result;
}
static void onRestack(WindowPtr w, WindowPtr old) {
    dirty = TRUE;
    ScreenPtr screen = w->drawable.pScreen;
    screen->RestackWindow = restack;
    if (screen->RestackWindow) screen->RestackWindow(w, old);
    restack = screen->RestackWindow;
    screen->RestackWindow = onRestack;
}

void lorieWindowModelInit(ScreenPtr screen) {
    realize = screen->RealizeWindow; screen->RealizeWindow = onRealize;
    unrealize = screen->UnrealizeWindow; screen->UnrealizeWindow = onUnrealize;
    destroy = screen->DestroyWindow; screen->DestroyWindow = onDestroy;
    position = screen->PositionWindow; screen->PositionWindow = onPosition;
    restack = screen->RestackWindow; screen->RestackWindow = onRestack;
    AddCallback(&PropertyStateCallback, propertyChanged, NULL);
}
