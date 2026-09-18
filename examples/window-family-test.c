#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/window_family.h"
#include "../lorie/src/main/cpp/lorie/window_inspection_walk.h"
#include "../lorie/src/main/cpp/lorie/window_inspection.h"

static WindowRec root, owner, input, dialog, dialogInput, nested, nestedInput, other, otherInput, frame;
static WindowPtr dialogOwner, nestedOwner;

static WindowPtr transientFor(WindowPtr window) {
    if (window == &dialog) return dialogOwner;
    if (window == &nested) return nestedOwner;
    return NULL;
}

static Bool belongs(WindowPtr window) {
    return lorieWindowFamilyContains(window, &owner, &root, transientFor);
}

static WindowPtr inspected[16];
static unsigned inspectedCount;
static Bool inspectionMember(WindowPtr window, WindowPtr selected) {
    assert(selected == &owner);
    return window == &other || belongs(window); // Models a compositor-associated group popup.
}
static void emit(WindowPtr window, void* unused) {
    (void)unused;
    assert(inspectedCount < 16);
    inspected[inspectedCount++] = window;
}

int main(void) {
    owner.parent = dialog.parent = nested.parent = other.parent = frame.parent = &root;
    input.parent = &owner;
    dialogInput.parent = &dialog;
    nestedInput.parent = &nested;
    otherInput.parent = &other;
    dialogOwner = &owner;
    nestedOwner = &dialog;
    assert(belongs(&owner));
    assert(belongs(&input));
    assert(belongs(&dialog));
    // A hidden 1x1 focus child must preserve the dialog's focus and stacking.
    assert(belongs(&dialogInput));
    assert(belongs(&nestedInput));
    assert(!belongs(&other));
    assert(!belongs(&otherInput));
    assert(!belongs(&root));
    assert(!belongs(NULL));

    // WM frames are not owned by their children; reparenting must not hide a
    // client's transient link, and a link may itself target a focus child.
    dialog.parent = &frame;
    assert(belongs(&dialogInput));
    assert(!belongs(&frame));
    nestedOwner = &dialogInput;
    assert(belongs(&nestedInput));

    dialogOwner = NULL;
    assert(!belongs(&dialogInput));
    dialogOwner = &root;
    assert(!belongs(&dialogInput));
    dialogOwner = &dialog;
    assert(!belongs(&dialogInput));
    dialogOwner = &nested;
    nestedOwner = &dialog;
    assert(!belongs(&nestedInput));

    WindowRec deep[65] = {0};
    for (unsigned i = 0; i < 64; i++) deep[i].parent = &deep[i + 1];
    deep[64].parent = &owner;
    assert(!belongs(&deep[0]));
    dialogOwner = &owner; nestedOwner = &dialog;
    root.lastChild = &owner; owner.prevSib = &dialog; dialog.prevSib = &other;
    owner.lastChild = &input; dialog.lastChild = &dialogInput; other.lastChild = &otherInput;
    dialog.parent = &root;
    LorieInspectionWalk result = lorieInspectFamily(&root, &owner, 16, inspectionMember, emit, NULL);
    assert(result.count == 6 && !result.truncated);
    assert(inspected[0] == &owner && inspected[1] == &input && inspected[3] == &dialogInput);
    assert(inspected[5] == &otherInput); // Hidden descendants of a group popup are retained too.
    inspectedCount = 0;
    result = lorieInspectFamily(&root, &owner, 1, inspectionMember, emit, NULL);
    assert(result.count == 1 && result.truncated && inspected[0] == &owner);
    inspectedCount = 0;
    result = lorieInspectFamily(&root, &owner, 6, inspectionMember, emit, NULL);
    assert(result.count == 6 && !result.truncated); // Exactly at the limit is complete.
    static WindowRec unrelated[8193];
    for (unsigned i = 0; i < 8193; i++) {
        unrelated[i].parent = &root;
        unrelated[i].prevSib = i + 1 < 8193 ? &unrelated[i + 1] : &owner;
    }
    root.lastChild = unrelated;
    inspectedCount = 0;
    result = lorieInspectFamily(&root, &owner, 16, inspectionMember, emit, NULL);
    assert(result.count == 1 && result.truncated); // Bound unrelated-tree traversal too.
    assert(sizeof(LorieInspectionNode) + 8 <= 280); // Do not enlarge every input/frame packet.
    puts("X11 focus-child ancestry, transient chains, WM frames and cycle bounds passed");
    return 0;
}
