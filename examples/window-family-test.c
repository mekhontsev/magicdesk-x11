#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/window_family.h"

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
    puts("X11 focus-child ancestry, transient chains, WM frames and cycle bounds passed");
    return 0;
}
