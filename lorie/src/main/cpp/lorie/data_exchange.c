#include <dix-config.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <X11/Xatom.h>
#include <windowstr.h>
#include <selection.h>
#include <property.h>
#include <propertyst.h>
#include <xacestr.h>
#include <inputstr.h>
#include <inpututils.h>
#include "lorie.h"

extern ScreenPtr pScreenPtr;
extern DeviceIntPtr lorieMouse;

#define MAX_PENDING 16
/* A protocol deadline, not polling: an abandoned selection must release its FD. */
#define TRANSFER_DEADLINE 30000

typedef struct {
    uint32_t generation;
    Window owner;
    TimeStamp time;
    Atom targets[LORIE_DATA_MAX_TARGETS];
    unsigned count;
    Bool local;
} Offer;

typedef struct {
    Bool used, incoming, incremental, targets;
    uint32_t serial, generation, ticket;
    Bool queued;
    int channel, fd;
    Window requestor;
    Atom target, property;
    CARD32 time;
    size_t size, offset;
    OsTimerPtr timer;
} Transfer;

static Offer offers[2];
static Transfer transfers[MAX_PENDING];
static Window bridge;
static uint32_t sequence, requestSequence, transferSequence, pointerOutput, pointerWindow;
static Bool enabled, clipboardActive, pointerDown, movingProperty;
static Atom clipboard, xdnd, targets, timestamp, incr, dataProperty;
static Atom aware, enter, position, leave, drop, status, finished, copy, typeList, proxy;
static Window dragTarget, dragProxy, dragSource;
static uint32_t dragOutput;
static Bool dragAccepted, dragPassthrough;
static Bool dragPositionPending, dragDropPending;
static Bool dragMoveQueued;
static LorieDataEvent dragMove;
static unsigned dragVersion;
static Window catcher, exportedSource;
static Bool exportDropped, exportCompleted, exportSuccess;
static OsTimerPtr exportTimer;
static int (*previousSendEvent)(ClientPtr);
static int (*previousConvertSelection)(ClientPtr);

static void endExport(void);
static void dropDrag(void);
static void moveDrag(const LorieDataEvent* event);

static Atom atom(const char* name) { return MakeAtom(name, strlen(name), TRUE); }
static Atom selection(int channel) { return channel ? xdnd : clipboard; }

static WindowPtr windowFor(Window id) {
    WindowPtr window = NULL;
    return dixLookupWindow(&window, id, serverClient, DixReadAccess) == Success ? window : NULL;
}

static Window newWindow(void) {
    int error;
    WindowPtr win = CreateWindow(FakeClientID(0), pScreenPtr->root, 0, 0, 1, 1, 0,
            InputOnly, 0, NULL, 0, serverClient, CopyFromParent, &error);
    if (!win) return None;
    if (!AddResource(win->drawable.id, RT_WINDOW, win)) { DeleteWindow(win, win->drawable.id); return None; }
    return win->drawable.id;
}

static void emit(int operation, int channel, uint32_t serial, uint32_t generation,
        const char* mime, int fd, uint32_t output, uint32_t window, int x) {
    LorieDataEvent event = {.t = EVENT_DATA, .operation = operation, .channel = channel,
        .serial = serial, .offer = generation, .output = output, .window = window, .x = x};
    if (mime) snprintf(event.mime, sizeof(event.mime), "%s", mime);
    lorieSendDataEvent(&event, fd);
}

static void message(Window destination, Window actual, Atom type,
        CARD32 a, CARD32 b, CARD32 c, CARD32 d, CARD32 e) {
    WindowPtr win = windowFor(destination);
    if (!win || CLIENT_ID(destination) == 0) return;
    xEvent event = {0};
    event.u.u.type = ClientMessage;
    event.u.u.detail = 32;
    event.u.clientMessage.window = actual;
    event.u.clientMessage.u.l.type = type;
    event.u.clientMessage.u.l.longs0 = a;
    event.u.clientMessage.u.l.longs1 = b;
    event.u.clientMessage.u.l.longs2 = c;
    event.u.clientMessage.u.l.longs3 = d;
    event.u.clientMessage.u.l.longs4 = e;
    WriteEventsToClient(wClient(win), 1, &event);
}

static void notify(Transfer* transfer, Bool success) {
    WindowPtr win = windowFor(transfer->requestor);
    if (!win || CLIENT_ID(transfer->requestor) == 0) return;
    xEvent event = {0};
    event.u.u.type = SelectionNotify;
    event.u.selectionNotify.time = transfer->time;
    event.u.selectionNotify.requestor = transfer->requestor;
    event.u.selectionNotify.selection = selection(transfer->channel);
    event.u.selectionNotify.target = transfer->target;
    event.u.selectionNotify.property = success ? transfer->property : None;
    WriteEventsToClient(wClient(win), 1, &event);
}

static void release(Transfer* transfer) {
    if (!transfer->used) return;
    transfer->used = FALSE;
    if (transfer->timer) { TimerFree(transfer->timer); transfer->timer = NULL; }
    if (transfer->fd >= 0) close(transfer->fd);
    if (transfer->incoming && transfer->requestor) FreeResource(transfer->requestor, RT_NONE);
    transfer->fd = -1;
}

static void fail(Transfer* transfer) {
    if (transfer->incoming) {
        if (!transfer->targets) emit(LORIE_DATA_REPLY, transfer->channel, transfer->serial,
                transfer->generation, NameForAtom(transfer->target), -1, 0, 0, 0);
    } else if (!transfer->incremental) notify(transfer, FALSE);
    else {
        WindowPtr win = windowFor(transfer->requestor);
        if (win) dixChangeWindowProperty(serverClient, win, transfer->property, transfer->target,
                8, PropModeReplace, 0, NULL, TRUE);
    }
    release(transfer);
}

static CARD32 expired(__unused OsTimerPtr timer, __unused CARD32 now, void* closure) {
    Transfer* transfer = closure;
    transfer->timer = NULL;
    TimerFree(timer);
    fail(transfer);
    return 0;
}

static Transfer* allocate(Bool incoming, int channel, uint32_t serial, uint32_t generation) {
    for (unsigned i = 0; i < MAX_PENDING; i++) if (!transfers[i].used) {
        Transfer* transfer = &transfers[i];
        *transfer = (Transfer){.used = TRUE, .incoming = incoming, .channel = channel,
                .serial = serial, .generation = generation, .ticket = ++transferSequence, .fd = -1};
        transfer->timer = TimerSet(NULL, 0, TRANSFER_DEADLINE, expired, transfer);
        return transfer;
    }
    return NULL;
}

static int bytesFile(const void* bytes, size_t size) {
    int fd = memfd_create("x11-content", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (size && pwrite(fd, bytes, size, 0) != (ssize_t)size) { close(fd); return -1; }
    return fd;
}

static Bool supportedName(const char* name) {
    return name && strlen(name) < 128 && (strchr(name, '/') || !strcmp(name, "UTF8_STRING")
            || !strcmp(name, "STRING") || !strcmp(name, "TEXT"));
}

static void publishTargets(int channel) {
    Offer* offer = &offers[channel];
    char names[LORIE_DATA_MAX_TARGETS * 128];
    size_t size = 0;
    for (unsigned i = 0; i < offer->count; i++) {
        const char* name = NameForAtom(offer->targets[i]);
        if (supportedName(name)) size += snprintf(names + size, sizeof(names) - size, "%s\n", name);
    }
    int fd = bytesFile(names, size);
    emit(LORIE_DATA_OFFER, channel, 0, offer->generation, NULL, fd,
            channel ? pointerOutput : 0, offer->owner, 0);
    if (fd >= 0) close(fd);
}

static void request(int channel, uint32_t serial, uint32_t generation, Atom target, Bool list) {
    Offer* offer = &offers[channel];
    Selection* owner = NULL;
    if (offer->local || offer->generation != generation ||
            dixLookupSelection(&owner, selection(channel), serverClient, DixReadAccess) != Success ||
            !owner->client || owner->window != offer->owner || !owner->window) {
        if (!list) emit(LORIE_DATA_REPLY, channel, serial, generation, NameForAtom(target), -1, 0, 0, 0);
        return;
    }
    Transfer* transfer = allocate(TRUE, channel, serial, generation);
    if (!transfer) {
        if (!list) emit(LORIE_DATA_REPLY, channel, serial, generation, NameForAtom(target), -1, 0, 0, 0);
        return;
    }
    transfer->target = target;
    transfer->targets = list;
    transfer->property = dataProperty;
    transfer->requestor = newWindow();
    if (!transfer->requestor) { fail(transfer); return; }
    xEvent event = {0};
    event.u.u.type = SelectionRequest;
    event.u.selectionRequest.owner = owner->window;
    event.u.selectionRequest.time = currentTime.milliseconds;
    event.u.selectionRequest.requestor = transfer->requestor;
    event.u.selectionRequest.selection = selection(channel);
    event.u.selectionRequest.target = target;
    event.u.selectionRequest.property = transfer->property;
    WriteEventsToClient(owner->client, 1, &event);
}

static void receive(Transfer* transfer, Bool initial) {
    WindowPtr win = windowFor(transfer->requestor);
    PropertyPtr prop = NULL;
    if (!win || dixLookupProperty(&prop, win, transfer->property, serverClient, DixReadAccess) != Success) {
        fail(transfer); return;
    }
    if (transfer->generation != offers[transfer->channel].generation) { fail(transfer); return; }
    if (initial && prop->type == incr && prop->format == 32 && prop->size == 1) {
        if (transfer->targets || *(CARD32*)prop->data > LORIE_DATA_MAX_BYTES) { fail(transfer); return; }
        transfer->fd = bytesFile(NULL, 0);
        if (transfer->fd < 0) { fail(transfer); return; }
        transfer->incremental = TRUE;
        DeleteProperty(serverClient, win, transfer->property);
        return;
    }
    if (transfer->targets) {
        if (prop->type != XA_ATOM || prop->format != 32 || prop->size > 4096) { fail(transfer); return; }
        Offer* offer = &offers[transfer->channel];
        offer->count = 0;
        CARD32* values = prop->data;
        for (unsigned i = 0; i < prop->size && offer->count < LORIE_DATA_MAX_TARGETS; i++)
            if (ValidAtom(values[i]) && supportedName(NameForAtom(values[i]))) offer->targets[offer->count++] = values[i];
        publishTargets(transfer->channel);
        release(transfer);
        return;
    }
    if (prop->format != 8 || prop->size > LORIE_DATA_MAX_BYTES - transfer->size) { fail(transfer); return; }
    if (transfer->fd < 0) transfer->fd = bytesFile(NULL, 0);
    if (transfer->fd < 0 || (prop->size && pwrite(transfer->fd, prop->data,
            prop->size, transfer->size) != (ssize_t)prop->size)) { fail(transfer); return; }
    transfer->size += prop->size;
    Bool complete = !transfer->incremental || !prop->size;
    if (complete) {
        emit(LORIE_DATA_REPLY, transfer->channel, transfer->serial, transfer->generation,
                NameForAtom(transfer->target), transfer->fd, 0, 0, 1);
        release(transfer);
    } else DeleteProperty(serverClient, win, transfer->property);
}

static Bool sendChunk(Transfer* transfer) {
    WindowPtr win = windowFor(transfer->requestor);
    if (!win) { release(transfer); return FALSE; }
    unsigned char bytes[LORIE_DATA_CHUNK];
    size_t length = min(sizeof(bytes), transfer->size - transfer->offset);
    if (length && pread(transfer->fd, bytes, length, transfer->offset) != (ssize_t)length) { fail(transfer); return FALSE; }
    transfer->offset += length;
    movingProperty = TRUE;
    int rc = dixChangeWindowProperty(serverClient, win, transfer->property, transfer->target,
            8, PropModeReplace, length, bytes, TRUE);
    movingProperty = FALSE;
    if (transfer->incremental && (rc != Success || !length)) release(transfer);
    return rc == Success;
}

typedef struct { Transfer* transfer; uint32_t ticket; } PropertyWork;

static Bool propertyWork(__unused ClientPtr client, void* closure) {
    PropertyWork* work = closure;
    Transfer* t = work->transfer;
    if (enabled && t->used && t->ticket == work->ticket) {
        t->queued = FALSE;
        if (t->incoming) receive(t, FALSE); else sendChunk(t);
    }
    free(work);
    return TRUE;
}

static void propertyChanged(__unused CallbackListPtr* list, __unused void* closure, void* args) {
    if (!enabled || movingProperty) return;
    PropertyStateRec* info = args;
    for (unsigned i = 0; i < MAX_PENDING; i++) {
        Transfer* t = &transfers[i];
        if (!t->used || !t->incremental || t->requestor != info->win->drawable.id || t->property != info->prop->propertyName) continue;
        // Property callbacks run before Xorg finishes using the window/property.
        // Defer mutation and disposal until the originating request has returned.
        if (!t->queued && ((t->incoming && info->state == PropertyNewValue) ||
                (!t->incoming && info->state == PropertyDelete))) {
            PropertyWork* work = malloc(sizeof(*work));
            if (!work) break;
            *work = (PropertyWork){t, t->ticket};
            t->queued = QueueWorkProc(propertyWork, NULL, work);
            if (!t->queued) free(work);
        }
        break;
    }
}

static void selectionChanged(__unused CallbackListPtr* list, __unused void* closure, void* args) {
    if (!enabled) return;
    SelectionInfoRec* info = args;
    int channel = info->selection->selection == clipboard ? 0 : info->selection->selection == xdnd ? 1 : -1;
    if (channel < 0 || info->selection->window == bridge) return;
    if (channel == 1 && exportedSource && info->selection->window != exportedSource) endExport();
    for (unsigned i = 0; i < MAX_PENDING; i++) if (transfers[i].used && transfers[i].channel == channel) fail(&transfers[i]);
    Offer* offer = &offers[channel];
    *offer = (Offer){.generation = ++sequence, .owner = info->selection->window,
            .time = info->selection->lastTimeChanged};
    if (info->kind != SelectionSetOwner || !offer->owner) {
        if (channel) emit(LORIE_DATA_CANCEL, 1, 0, offer->generation, NULL, -1, pointerOutput, pointerWindow, 0);
        return;
    }
    if (channel ? pointerDown : clipboardActive) request(channel, 0, offer->generation, targets, TRUE);
}

static Bool own(int channel) {
    Selection* sel = NULL;
    int rc = dixLookupSelection(&sel, selection(channel), serverClient, DixSetAttrAccess);
    if (rc == BadMatch) {
        sel = dixAllocateObjectWithPrivates(Selection, PRIVATE_SELECTION);
        if (!sel) return FALSE;
        sel->selection = selection(channel);
        if (XaceHookSelectionAccess(serverClient, &sel, DixCreateAccess | DixSetAttrAccess) != Success) { free(sel); return FALSE; }
        sel->next = CurrentSelections;
        CurrentSelections = sel;
    } else if (rc != Success) return FALSE;
    if (sel->client && sel->client != serverClient && sel->window) {
        xEvent event = {0};
        event.u.u.type = SelectionClear;
        event.u.selectionClear.time = currentTime.milliseconds;
        event.u.selectionClear.window = sel->window;
        event.u.selectionClear.atom = sel->selection;
        WriteEventsToClient(sel->client, 1, &event);
    }
    sel->window = bridge;
    sel->pWin = windowFor(bridge);
    sel->client = serverClient;
    sel->lastTimeChanged = currentTime;
    SelectionInfoRec info = {sel, serverClient, SelectionSetOwner};
    CallCallbacks(&SelectionCallback, &info);
    return TRUE;
}

static int convert(ClientPtr client) {
    REQUEST(xConvertSelectionReq);
    REQUEST_SIZE_MATCH(xConvertSelectionReq);
    int channel = stuff->selection == clipboard ? 0 : stuff->selection == xdnd ? 1 : -1;
    if (!enabled || channel < 0 || !offers[channel].local) return previousConvertSelection(client);
    Transfer* t = allocate(FALSE, channel, ++requestSequence, offers[channel].generation);
    if (!t) return BadAlloc;
    t->requestor = stuff->requestor;
    t->target = stuff->target;
    t->property = stuff->property ? stuff->property : stuff->target;
    t->time = stuff->time;
    WindowPtr win = NULL;
    int rc = dixLookupWindow(&win, t->requestor, client, DixSetAttrAccess);
    if (rc != Success || !ValidAtom(t->target) || !ValidAtom(t->property)) { fail(t); return Success; }
    if (t->target == targets) {
        Atom values[LORIE_DATA_MAX_TARGETS + 2] = {targets, timestamp};
        memcpy(values + 2, offers[channel].targets, offers[channel].count * sizeof(Atom));
        rc = dixChangeWindowProperty(serverClient, win, t->property, XA_ATOM, 32,
                PropModeReplace, offers[channel].count + 2, values, TRUE);
        notify(t, rc == Success); release(t);
    } else if (t->target == timestamp) {
        CARD32 time = offers[channel].time.milliseconds;
        rc = dixChangeWindowProperty(serverClient, win, t->property, XA_INTEGER, 32, PropModeReplace, 1, &time, TRUE);
        notify(t, rc == Success); release(t);
    } else {
        Bool found = FALSE;
        for (unsigned i = 0; i < offers[channel].count; i++) found |= offers[channel].targets[i] == t->target;
        if (!found) fail(t);
        else emit(LORIE_DATA_REQUEST, channel, t->serial, t->generation, NameForAtom(t->target), -1, 0, 0, 0);
    }
    return Success;
}

static int sendEvent(ClientPtr client) {
    REQUEST(xSendEventReq);
    REQUEST_SIZE_MATCH(xSendEventReq);
    if (enabled && stuff->event.u.u.type == SelectionNotify) {
        for (unsigned i = 0; i < MAX_PENDING; i++) {
            Transfer* t = &transfers[i];
            if (t->used && t->incoming && t->requestor == stuff->event.u.selectionNotify.requestor &&
                    t->target == stuff->event.u.selectionNotify.target &&
                    selection(t->channel) == stuff->event.u.selectionNotify.selection) {
                if (stuff->event.u.selectionNotify.property == t->property) receive(t, TRUE);
                else fail(t);
                return Success;
            }
        }
    }
    if (enabled && stuff->event.u.u.type == ClientMessage && stuff->event.u.u.detail == 32) {
        xEvent* e = &stuff->event;
        Atom type = e->u.clientMessage.u.l.type;
        if (exportedSource && e->u.clientMessage.window == catcher &&
                e->u.clientMessage.u.l.longs0 == exportedSource) {
            if (type == position) {
                message(exportedSource, exportedSource, status, catcher, 3, 0, 0, copy);
                return Success;
            }
            if (type == enter || type == leave) return Success;
            if (type == drop) {
                exportDropped = TRUE;
                if (exportCompleted) endExport();
                return Success;
            }
        }
        if (e->u.clientMessage.window == dragSource && dragTarget &&
                e->u.clientMessage.u.l.longs0 == dragTarget) {
            if (type == status) {
                dragPositionPending = FALSE;
                dragAccepted = (e->u.clientMessage.u.l.longs1 & 1) && e->u.clientMessage.u.l.longs4 == copy;
                emit(LORIE_DATA_STATUS, 1, 0, offers[1].generation, NULL, -1, dragOutput, dragTarget, dragAccepted);
                if (dragMoveQueued) {
                    dragMoveQueued = FALSE;
                    moveDrag(&dragMove);
                }
                if (dragDropPending) dropDrag();
                return Success;
            } else if (type == finished) {
                emit(LORIE_DATA_FINISH, 1, 0, offers[1].generation, NULL, -1, dragOutput, dragTarget,
                        dragVersion < 5 || (e->u.clientMessage.u.l.longs1 & 1) != 0);
                dragTarget = None;
                return Success;
            }
        }
    }
    return previousSendEvent(client);
}

static Bool readTargets(Offer* offer, int fd) {
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || st.st_size < 0 || st.st_size >= LORIE_DATA_MAX_TARGETS * 128) return FALSE;
    char bytes[LORIE_DATA_MAX_TARGETS * 128];
    if (pread(fd, bytes, st.st_size, 0) != st.st_size) return FALSE;
    bytes[st.st_size] = 0;
    char* save = NULL;
    for (char* name = strtok_r(bytes, "\n", &save); name; name = strtok_r(NULL, "\n", &save)) {
        if (offer->count == LORIE_DATA_MAX_TARGETS || !supportedName(name)) return FALSE;
        offer->targets[offer->count++] = atom(name);
    }
    return TRUE;
}

static Window childAt(WindowPtr parent, int x, int y) {
    for (WindowPtr child = parent->firstChild; child; child = child->nextSib) {
        if (!child->mapped || child->drawable.id == bridge || child->drawable.id == catcher) continue;
        if (x >= child->drawable.x && y >= child->drawable.y &&
                x < child->drawable.x + child->drawable.width && y < child->drawable.y + child->drawable.height) {
            Window nested = childAt(child, x, y);
            return nested ? nested : child->drawable.id;
        }
    }
    return parent->drawable.id;
}

static unsigned awareVersion(WindowPtr win) {
    PropertyPtr prop;
    if (dixLookupProperty(&prop, win, aware, serverClient, DixReadAccess) != Success ||
            prop->type != XA_ATOM || prop->format != 32 || prop->size < 1) return 0;
    return min(5, *(CARD32*)prop->data);
}

static Window destinationProxy(Window target) {
    WindowPtr win = windowFor(target);
    PropertyPtr prop;
    if (win && dixLookupProperty(&prop, win, proxy, serverClient, DixReadAccess) == Success &&
            prop->type == XA_WINDOW && prop->format == 32 && prop->size == 1) {
        Window id = *(CARD32*)prop->data;
        win = windowFor(id);
        if (win && dixLookupProperty(&prop, win, proxy, serverClient, DixReadAccess) == Success &&
                prop->type == XA_WINDOW && prop->format == 32 && prop->size == 1 && *(CARD32*)prop->data == id) return id;
    }
    return target;
}

static void moveDrag(const LorieDataEvent* event) {
    if (dragPositionPending) { dragMove = *event; dragMoveQueued = TRUE; return; }
    WindowPtr selected;
    int x, y;
    if (!lorieOutputPoint(event->output, event->window, event->x, event->y, &selected, &x, &y)) return;
    WindowPtr win = windowFor(childAt(selected, x, y));
    while (win && !awareVersion(win)) win = win->parent;
    Window next = win ? win->drawable.id : None;
    if (next != dragTarget) {
        if (dragTarget) message(dragProxy, dragTarget, leave, dragSource, 0, 0, 0, 0);
        dragTarget = next;
        dragProxy = next ? destinationProxy(next) : None;
        dragAccepted = FALSE;
        dragPositionPending = FALSE;
        if (next) {
            dragVersion = awareVersion(win);
            Atom* types = offers[1].targets;
            message(dragProxy, next, enter, dragSource, (dragVersion << 24) | (offers[1].count > 3),
                    offers[1].count > 0 ? types[0] : 0, offers[1].count > 1 ? types[1] : 0, offers[1].count > 2 ? types[2] : 0);
        }
    }
    dragOutput = event->output;
    if (dragTarget) {
        dragPositionPending = TRUE;
        message(dragProxy, dragTarget, position, dragSource, 0,
                ((uint32_t)x << 16) | (y & 0xffff), currentTime.milliseconds, copy);
    }
}

static void dropDrag(void) {
    if (dragPositionPending) { dragDropPending = TRUE; return; }
    dragDropPending = FALSE;
    if (dragTarget && dragAccepted) message(dragProxy, dragTarget, drop, dragSource, 0, currentTime.milliseconds, 0, 0);
    else emit(LORIE_DATA_FINISH, 1, 0, offers[1].generation, NULL, -1, dragOutput, dragTarget, 0);
}

static void endExport(void) {
    if (exportTimer) { TimerFree(exportTimer); exportTimer = NULL; }
    if (exportedSource) message(exportedSource, exportedSource, finished, catcher,
            exportDropped && exportCompleted && exportSuccess ? 1 : 0, copy, 0, 0);
    exportedSource = None;
    if (catcher) { FreeResource(catcher, RT_NONE); catcher = None; }
    if (pointerDown) QueuePointerEvents(lorieMouse, ButtonRelease, 1, POINTER_RELATIVE, NULL);
    pointerDown = FALSE;
}

static CARD32 exportExpired(__unused OsTimerPtr timer, __unused CARD32 now, __unused void* closure) {
    exportTimer = NULL;
    TimerFree(timer);
    exportSuccess = FALSE;
    endExport();
    return 0;
}

static void beginExport(void) {
    if (exportedSource || offers[1].local || !offers[1].owner || !pointerDown) return;
    catcher = newWindow();
    WindowPtr win = windowFor(catcher);
    if (!win) return;
    /* An actual XDND destination inside Xorg keeps toolkit pointer grabs and
     * drop completion intact while Android owns the cross-window gesture. */
    XID size[] = {(XID)pScreenPtr->width, (XID)pScreenPtr->height};
    ConfigureWindow(win, CWWidth | CWHeight, size, serverClient);
    CARD32 version = 5;
    dixChangeWindowProperty(serverClient, win, aware, XA_ATOM, 32, PropModeReplace, 1, &version, TRUE);
    MapWindow(win, serverClient);
    exportedSource = offers[1].owner;
    exportDropped = exportCompleted = exportSuccess = FALSE;
    exportTimer = TimerSet(NULL, 0, TRANSFER_DEADLINE, exportExpired, NULL);
    ValuatorMask mask;
    valuator_mask_zero(&mask);
    valuator_mask_set_double(&mask, 0, 0);
    valuator_mask_set_double(&mask, 1, 0);
    QueuePointerEvents(lorieMouse, MotionNotify, 0, POINTER_RELATIVE, &mask);
}

void lorieDataPointer(uint32_t output, uint32_t window, int button, Bool down) {
    if (button == 1) { pointerDown = down; pointerOutput = output; pointerWindow = window; }
}

void lorieDataCommand(const LorieDataEvent* event, int fd) {
    int channel = event->channel;
    if (channel > 1 || !pScreenPtr || !pScreenPtr->root) goto done;
    if (event->operation == LORIE_DATA_ENABLE) {
        enabled = TRUE;
        clipboardActive = event->x != 0;
        lorieEnableClipboardSync(FALSE);
        if (!bridge) bridge = newWindow();
        goto done;
    }
    if (!enabled || !bridge) goto done;
    Offer* offer = &offers[channel];
    switch (event->operation) {
        case LORIE_DATA_OFFER: {
            Offer next = {.generation = event->offer, .local = TRUE, .owner = bridge, .time = currentTime};
            if (readTargets(&next, fd)) { *offer = next; own(channel); }
            break;
        }
        case LORIE_DATA_READ:
            if (memchr(event->mime, 0, sizeof(event->mime))) request(channel, event->serial, event->offer, atom(event->mime), FALSE);
            break;
        case LORIE_DATA_REPLY:
            for (unsigned i = 0; i < MAX_PENDING; i++) {
                Transfer* t = &transfers[i];
                if (!t->used || t->incoming || t->serial != event->serial || t->generation != event->offer || t->channel != channel) continue;
                struct stat st;
                if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > LORIE_DATA_MAX_BYTES) { fail(t); break; }
                t->fd = fd; fd = -1;
                t->size = st.st_size;
                WindowPtr win = windowFor(t->requestor);
                if (!win) { release(t); break; }
                if (t->size > LORIE_DATA_CHUNK) {
                    CARD32 size = t->size;
                    t->incremental = TRUE;
                    int rc = dixChangeWindowProperty(serverClient, win, t->property, incr, 32, PropModeReplace, 1, &size, TRUE);
                    notify(t, rc == Success);
                    if (rc != Success) release(t);
                } else {
                    Bool success = sendChunk(t);
                    if (t->used) notify(t, success);
                    release(t);
                }
                break;
            }
            break;
        case LORIE_DATA_ENTER:
            dragPassthrough = event->x != 0 && !offer->local && offer->generation == event->offer;
            dragSource = bridge;
            if (!dragPassthrough && !offer->local) break;
            if (dragSource == bridge) {
                WindowPtr win = windowFor(bridge);
                dixChangeWindowProperty(serverClient, win, typeList, XA_ATOM, 32, PropModeReplace, offer->count, offer->targets, TRUE);
            }
            dragTarget = None; dragOutput = event->output; dragAccepted = FALSE;
            dragMoveQueued = dragDropPending = dragPositionPending = FALSE;
            break;
        case LORIE_DATA_MOVE: moveDrag(event); break;
        case LORIE_DATA_LEAVE:
            if (dragTarget) message(dragProxy, dragTarget, leave, dragSource, 0, 0, 0, 0);
            dragTarget = None; dragAccepted = FALSE;
            dragMoveQueued = dragDropPending = dragPositionPending = FALSE;
            break;
        case LORIE_DATA_DROP:
            dropDrag();
            break;
        case LORIE_DATA_FINISH:
        case LORIE_DATA_CANCEL:
            if (exportedSource) {
                exportCompleted = TRUE;
                exportSuccess = event->operation == LORIE_DATA_FINISH && event->x;
                if (pointerDown) QueuePointerEvents(lorieMouse, ButtonRelease, 1, POINTER_RELATIVE, NULL);
                pointerDown = FALSE;
                if (exportDropped || !exportSuccess) endExport();
            }
            dragTarget = None; dragAccepted = FALSE;
            break;
        case LORIE_DATA_BEGIN: beginExport(); break;
    }
done:
    if (fd >= 0) close(fd);
}

void lorieDataReset(void) {
    endExport();
    enabled = clipboardActive = FALSE;
    for (unsigned i = 0; i < MAX_PENDING; i++) release(&transfers[i]);
    if (bridge) FreeResource(bridge, RT_NONE);
    bridge = dragTarget = dragSource = None;
    dragMoveQueued = dragDropPending = dragPositionPending = FALSE;
    pointerDown = FALSE;
    memset(offers, 0, sizeof(offers));
}

void lorieDataShutdown(void) {
    lorieDataReset();
    DeleteCallback(&SelectionCallback, selectionChanged, NULL);
    DeleteCallback(&PropertyStateCallback, propertyChanged, NULL);
    if (ProcVector[X_SendEvent] == sendEvent) ProcVector[X_SendEvent] = previousSendEvent;
    if (ProcVector[X_ConvertSelection] == convert) ProcVector[X_ConvertSelection] = previousConvertSelection;
}

void lorieDataInit(void) {
    clipboard = atom("CLIPBOARD"); xdnd = atom("XdndSelection");
    targets = atom("TARGETS"); timestamp = atom("TIMESTAMP"); incr = atom("INCR"); dataProperty = atom("_MAGICDESK_DATA");
    aware = atom("XdndAware"); enter = atom("XdndEnter"); position = atom("XdndPosition");
    leave = atom("XdndLeave"); drop = atom("XdndDrop"); status = atom("XdndStatus");
    finished = atom("XdndFinished"); copy = atom("XdndActionCopy"); typeList = atom("XdndTypeList"); proxy = atom("XdndProxy");
    previousSendEvent = ProcVector[X_SendEvent]; ProcVector[X_SendEvent] = sendEvent;
    previousConvertSelection = ProcVector[X_ConvertSelection]; ProcVector[X_ConvertSelection] = convert;
    if (!AddCallback(&SelectionCallback, selectionChanged, NULL) || !AddCallback(&PropertyStateCallback, propertyChanged, NULL))
        FatalError("Cannot observe embedded X11 data transfers\n");
}
