#include <dix-config.h>
#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/window_outputs.c"
#include "../lorie/src/main/cpp/lorie/data_exchange.c"

DeviceIntPtr lorieMouse, lorieKeyboard;
ClientPtr serverClient;
ClientPtr clients[MAXCLIENTS];
ScreenPtr pScreenPtr;
TimeStamp currentTime;
static WindowRec catcherWindow;
static int presses, releases, raises, motions;
static Bool exporting;
static xEvent notification;
static int notifications;
static LorieDataEvent lastData;
static int dataEvents;

pixman_bool_t pixman_region_contains_point(const pixman_region16_t* region, int x, int y, pixman_box16_t* box) {
    *box = region->extents;
    return x >= box->x1 && y >= box->y1 && x < box->x2 && y < box->y2;
}
Bool PointInBorderSize(WindowPtr win, int x, int y) {
    BoxRec box;
    return RegionContainsPoint(&win->borderSize, x, y, &box);
}

unsigned int ResourceClientBits(void) { return 8; }
void WriteEventsToClient(ClientPtr client, int count, xEventPtr events) {
    (void)client; assert(count == 1);
    notification = events[0]; notifications++;
}
OsTimerPtr TimerSet(OsTimerPtr timer, int flags, CARD32 millis, OsTimerCallback func, void* arg) {
    (void)timer; (void)flags; (void)millis; (void)func; (void)arg;
    return NULL;
}
void TimerFree(OsTimerPtr timer) { (void)timer; }
void FreeResource(XID id, RESTYPE type) { (void)id; (void)type; }
const char* NameForAtom(Atom id) { (void)id; return "text/uri-list"; }
void lorieSendDataEvent(const LorieDataEvent* event, int fd) { lastData = *event; dataEvents++; (void)fd; }
int dixLookupProperty(PropertyPtr* result, WindowPtr win, Atom property, ClientPtr client, Mask access) {
    (void)win; (void)client; (void)access;
    static CARD32 version = 5;
    static PropertyRec value;
    value.type = XA_ATOM; value.format = 32; value.size = 1; value.data = &version;
    *result = &value;
    return property == aware ? Success : BadMatch;
}
int dixChangeWindowProperty(ClientPtr client, WindowPtr win, Atom property, Atom type,
        int format, int mode, unsigned long size, const void* data, Bool sendevent) {
    (void)client; (void)win; (void)property; (void)type; (void)format;
    (void)mode; (void)size; (void)data; (void)sendevent;
    return Success;
}

int dixLookupWindow(WindowPtr* result, XID id, ClientPtr client, Mask access) {
    (void)client; (void)access;
    *result = id == catcher && catcher ? &catcherWindow : NULL;
    return *result ? Success : BadWindow;
}

int ConfigureWindow(WindowPtr window, Mask mask, XID* values, ClientPtr client) {
    (void)client;
    assert(window == &catcherWindow && mask == CWStackMode && values[0] == Above);
    raises++;
    return Success;
}

void valuator_mask_zero(ValuatorMask* mask) { memset(mask, 0, sizeof(*mask)); }
void valuator_mask_set_double(ValuatorMask* mask, int index, double value) {
    (void)mask;
    assert((index == 0 || index == 1) && value == 0);
}

void QueuePointerEvents(DeviceIntPtr device, int type, int button, int flags,
                        const ValuatorMask* mask) {
    (void)device; (void)flags; (void)mask;
    if (type == MotionNotify) {
        assert(button == 0 && raises == motions + 1);
        motions++;
        return;
    }
    assert(button == 1);
    if (type == ButtonPress) presses++;
    else {
        assert(type == ButtonRelease);
        if (exporting) assert(raises == 1 && motions == 1);
        releases++;
    }
}

int main(void) {
    static OutputSelection source, other;
    source.id = 1; source.window = 100; source.next = &other;
    other.id = 2; other.window = 200;
    selections = &source;

    // Export completion must clear the lease as well as the physical button.
    for (int i = 1; i <= 3; i++) {
        outputButton(&source, 1, TRUE);
        assert(presses == i);
        lorieReleaseOutputButton(1, 100, 1);
        assert(source.buttons == 0 && releases == i);
        lorieReleaseOutputButton(1, 100, 1);
        assert(releases == i);
    }

    // A stale output/window pair cannot release a rebound window's input.
    outputButton(&source, 1, TRUE);
    lorieReleaseOutputButton(1, 99, 1);
    lorieReleaseOutputButton(99, 100, 1);
    lorieReleaseOutputButton(1, 100, 0);
    lorieReleaseOutputButton(1, 100, 8);
    assert(source.buttons != 0 && releases == 3);

    // Finishing one drag must not release another view's held button.
    outputButton(&other, 1, TRUE);
    lorieReleaseOutputButton(1, 100, 1);
    assert(source.buttons == 0 && other.buttons != 0 && releases == 3);
    lorieReleaseOutputButton(2, 200, 1);
    assert(other.buttons == 0 && presses == 4 && releases == 4);

    // Restack/re-enter the catcher before releasing the original toolkit's
    // grab. Otherwise its native drop can duplicate the host-delivered drop.
    catcher = 300;
    exportedOutput = pointerOutput = source.id;
    exportedWindow = pointerWindow = source.window;
    pointerDown = TRUE;
    outputButton(&source, 1, TRUE);
    exporting = TRUE;
    releaseExportPointer();
    assert(!pointerDown && !source.buttons && !exportedOutput && !exportedWindow);
    assert(presses == 5 && releases == 5 && raises == 1 && motions == 1);
    releaseExportPointer();
    assert(releases == 5 && raises == 1 && motions == 1);
    puts("output input leases: ok");
    catcher = (1U << CLIENTOFFSET) | 1;
    catcherWindow.drawable.id = catcher;
    xdnd = 123;
    xConvertSelectionReq request = {.requestor = catcher, .selection = xdnd,
            .target = 456, .property = 789, .time = 42};
    for (unsigned i = 0; i < MAX_PENDING; i++) assert(outgoingTransfer(1, &request));
    for (unsigned i = 0; i < 50; i++) assert(!outgoingTransfer(1, &request));
    assert(notifications == 50 && notification.u.u.type == SelectionNotify);
    assert(notification.u.selectionNotify.requestor == catcher);
    assert(notification.u.selectionNotify.selection == xdnd);
    assert(notification.u.selectionNotify.target == request.target);
    assert(notification.u.selectionNotify.time == request.time);
    assert(notification.u.selectionNotify.property == None);
    release(&transfers[0]);
    assert(outgoingTransfer(1, &request));
    for (unsigned i = 0; i < MAX_PENDING; i++) release(&transfers[i]);
    puts("selection backpressure refuses excess requests without killing the client: ok");

    WindowRec root = {0}, overlay = {0}, target = {0};
    WindowOptRec shaped = {0};
    RegionRec shape = {0};
    root.drawable.id = 10; root.firstChild = &overlay;
    overlay.drawable.id = 11; overlay.mapped = TRUE;
    overlay.drawable.width = overlay.drawable.height = 100;
    overlay.nextSib = &target; overlay.optional = &shaped;
    target.drawable.id = 12; target.mapped = TRUE;
    target.drawable.width = target.drawable.height = 100;
    shaped.inputShape = &shape;
    assert(childAt(&root, 50, 50) == target.drawable.id);
    shape.extents = (BoxRec){0, 0, 100, 100};
    assert(childAt(&root, 50, 50) == overlay.drawable.id);
    overlay.drawable.x = 40;
    shape.extents = (BoxRec){20, 0, 100, 100};
    assert(childAt(&root, 50, 50) == target.drawable.id);
    assert(childAt(&root, 70, 50) == overlay.drawable.id);
    overlay.unhittable = TRUE;
    assert(childAt(&root, 70, 50) == target.drawable.id);
    overlay.unhittable = FALSE;
    shaped.inputShape = NULL; shaped.boundingShape = &shape;
    assert(childAt(&root, 70, 50) == target.drawable.id);
    puts("XDND hit testing respects input and bounding shapes and unhittable overlays: ok");

    ScreenRec screen = {.root = &root};
    pScreenPtr = &screen;
    source.next = NULL; source.window = catcher; source.contentWidth = source.contentHeight = 100;
    dragTarget = dragProxy = catcher; dragSource = 1;
    aware = 1000; position = 1001; drop = 1002; leave = 1003;
    dragMove = (LorieDataEvent){.output = source.id, .window = catcher, .x = 5000, .y = 5000};
    offers[1].generation = 10;
    Transfer* pending = outgoingTransfer(1, &request);
    int before = dataEvents;
    dropDrag();
    assert(dragDropPending && !dragPositionPending && dataEvents == before);
    release(pending);
    selectionFinished(pending, TRUE);
    assert(dragDropPending && dragPositionPending && dragDataRevision == 1);
    assert(notification.u.clientMessage.u.l.type == position);
    dragPositionPending = FALSE; dragAccepted = TRUE;
    dropDrag();
    assert(!dragDropPending && notification.u.clientMessage.u.l.type == drop);

    // Data can finish before the initial negative status arrives.
    dragAccepted = FALSE; dragPositionPending = dragDropPending = TRUE;
    pending = outgoingTransfer(1, &request);
    release(pending); selectionFinished(pending, TRUE);
    assert(dragDataRevision != dragPositionDataRevision);
    dragPositionPending = FALSE; dropDrag();
    assert(dragPositionPending && dragDataRevision == dragPositionDataRevision);
    // A final refusal without further data is terminal, not a retry loop.
    dragPositionPending = FALSE; dropDrag();
    assert(!dragDropPending && !dragTarget && lastData.operation == LORIE_DATA_FINISH && !lastData.x);

    dragTarget = dragProxy = catcher;
    pending = outgoingTransfer(1, &request);
    dropDrag(); assert(dragDropPending);
    release(pending); selectionFinished(pending, FALSE);
    assert(!dragDropPending && !dragTarget && lastData.operation == LORIE_DATA_FINISH && !lastData.x);
    puts("XDND waits for pending selection data and renegotiates by completion, never by delay: ok");
    return 0;
}
