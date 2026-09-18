#include <assert.h>
#include <stdlib.h>
#include "../lorie/src/main/cpp/lorie/screen_mode.h"

static void exactMode(int width, int height) {
    struct libxcvt_mode_info* mode = lorieVirtualMode(width, height, 60);
    assert(mode);
    assert(mode->hdisplay == (uint32_t)width);
    assert(mode->vdisplay == (uint32_t)height);
    assert(mode->hsync_start >= mode->hdisplay);
    assert(mode->hsync_end >= mode->hsync_start);
    assert(mode->htotal >= mode->hsync_end);
    assert(mode->vsync_start >= mode->vdisplay);
    assert(mode->vsync_end >= mode->vsync_start);
    assert(mode->vtotal >= mode->vsync_end);
    free(mode);
}

int main(void) {
    // The oversized file dialog used to request 3372 forever, receiving 3368.
    struct libxcvt_mode_info* physical = libxcvt_gen_mode_info(3372, 2466, 60, false, false);
    assert(physical && physical->hdisplay == 3368);
    free(physical);
    exactMode(3372, 2466);
    exactMode(1216, 1660);
    exactMode(1360, 768);
    exactMode(1366, 768);
    exactMode(16384, 16384);
    for (int width = 320; width <= 4096; width++) exactMode(width, 800);
    assert(!lorieVirtualMode(0, 800, 60));
    assert(!lorieVirtualMode(800, -1, 60));
    assert(!lorieVirtualMode(32768, 800, 60));
    assert(!lorieVirtualMode(800, 32768, 60));
    return 0;
}
