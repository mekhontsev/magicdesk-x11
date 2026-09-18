#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/window_size.h"

int main(void) {
    uint32_t hints[18] = {[0] = LORIE_HINT_MIN_SIZE | LORIE_HINT_MAX_SIZE,
            [5] = 644, [6] = 199, [7] = 644, [8] = 199};
    assert(lorieWindowSizeAxis(1216, NULL, 0, 0) == 1216);
    assert(lorieWindowSizeAxis(0, NULL, 0, 0) == 1);
    assert(lorieWindowSizeAxis(INT32_MAX, NULL, 0, 0) == LORIE_WINDOW_SIZE_LIMIT);
    assert(lorieWindowSizeAxis(1216, hints, 18, 0) == 644);
    assert(lorieWindowSizeAxis(2498, hints, 18, 1) == 199);
    assert(lorieWindowSizeAxis(100, hints, 18, 0) == 644);
    assert(lorieWindowSizeAxis(644, hints, 18, 0) == 644);
    // The older 15-word format carries the same minimum/maximum fields.
    assert(lorieWindowSizeAxis(1216, hints, 15, 0) == 644);
    for (size_t count = 0; count < 7; count++)
        assert(lorieWindowSizeAxis(100, hints, count, 0) == 100);
    assert(lorieWindowSizeAxis(1216, hints, 7, 0) == 1216);
    assert(lorieWindowSizeAxis(100, hints, 7, 0) == 644);

    hints[0] = LORIE_HINT_MIN_SIZE;
    assert(lorieWindowSizeAxis(1216, hints, 18, 0) == 1216);
    assert(lorieWindowSizeAxis(100, hints, 18, 0) == 644);
    hints[0] = LORIE_HINT_MAX_SIZE;
    assert(lorieWindowSizeAxis(100, hints, 18, 0) == 100);
    assert(lorieWindowSizeAxis(1216, hints, 18, 0) == 644);
    hints[0] = 0;
    assert(lorieWindowSizeAxis(1216, hints, 18, 0) == 1216);

    hints[0] = LORIE_HINT_MIN_SIZE | LORIE_HINT_MAX_SIZE;
    hints[7] = 100;
    assert(lorieWindowSizeAxis(1216, hints, 18, 0) == 1216);
    assert(lorieWindowSizeAxis(2498, hints, 18, 1) == 199);
    hints[5] = 0;
    hints[7] = UINT32_MAX;
    assert(lorieWindowSizeAxis(1216, hints, 18, 0) == 1216);
    hints[5] = UINT32_MAX;
    hints[7] = 0;
    assert(lorieWindowSizeAxis(1216, hints, 18, 0) == 1216);
    hints[5] = LORIE_WINDOW_SIZE_LIMIT + 1;
    hints[7] = INT32_MAX;
    assert(lorieWindowSizeAxis(1216, hints, 18, 0) == 1216);
    assert(lorieWindowSizeAxis(INT32_MAX, hints, 18, 0) == LORIE_WINDOW_SIZE_LIMIT);
    puts("X11 hosted minimum/maximum size constraints passed");
    return 0;
}
