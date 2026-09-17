#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/fullscreen_state.h"

int main(void) {
    LorieFullscreenState state = {0};
    assert(!lorieFullscreenRequest(&state, 3));
    assert(state.serial == 0);
    assert(lorieFullscreenRequest(&state, 1));
    assert(state.requested && !state.actual);
    uint32_t entry = state.serial;
    assert(lorieFullscreenRequest(&state, 2));
    assert(!state.requested);
    assert(!lorieFullscreenConfirm(&state, entry, true));
    assert(!state.actual && !state.requested);
    assert(lorieFullscreenRequest(&state, 2));
    assert(lorieFullscreenConfirm(&state, state.serial, true));
    assert(state.requested && state.actual);
    // Manual host restoration is authoritative, but an old acknowledgement is not.
    assert(lorieFullscreenConfirm(&state, state.serial, false));
    assert(!state.actual && !state.requested);
    assert(lorieFullscreenRequest(&state, 1));
    entry = state.serial;
    assert(lorieFullscreenRequest(&state, 1));
    assert(state.serial != entry);
    assert(!lorieFullscreenConfirm(&state, entry, true));
    assert(lorieFullscreenConfirm(&state, state.serial, false));
    assert(!state.requested);
    state.serial = UINT32_MAX;
    assert(lorieFullscreenRequest(&state, 0));
    assert(state.serial == 1);
    puts("fullscreen state tests passed");
}
