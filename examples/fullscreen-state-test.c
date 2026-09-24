#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/fullscreen_state.h"
#include "../lorie/src/main/cpp/lorie/maximized_state.h"

int main(void) {
    LorieMaximizedState maximized = {.serial = 7};
    assert(lorieMaximizedRequest(&maximized, 1, 1) && maximized.requested == 1);
    assert(lorieMaximizedRequest(&maximized, 1, 2) && maximized.requested == 3);
    assert(!lorieMaximizedConfirm(&maximized, 6, 0) && maximized.requested == 3);
    assert(lorieMaximizedConfirm(&maximized, 7, 3) && maximized.actual == 3);
    assert(lorieMaximizedRequest(&maximized, 2, 3) && maximized.requested == 0);
    assert(!lorieMaximizedRequest(&maximized, 1, 4) && !lorieMaximizedRequest(&maximized, 4, 3));
    assert(!lorieMaximizedConfirm(&maximized, 7, 8));
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
