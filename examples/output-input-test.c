#include <dix-config.h>
#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/window_outputs.c"
#include "../lorie/src/main/cpp/lorie/data_exchange.c"

DeviceIntPtr lorieMouse, lorieKeyboard;
ClientPtr serverClient;
static WindowRec catcherWindow;
static int presses, releases, raises, motions;
static Bool exporting;

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
    return 0;
}
