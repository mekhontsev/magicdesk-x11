#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/window_size.h"

int main(void) {
    uint32_t all[18] = {[0]=LORIE_HINT_BASE_SIZE|LORIE_HINT_RESIZE_INC|LORIE_HINT_ASPECT,
        [9]=8,[10]=16,[11]=4,[12]=3,[13]=16,[14]=9,[15]=10,[16]=20};
    LorieWindowConstraints limits=lorieWindowSizeHints(all,18);
    assert(limits.minWidth==10 && limits.minHeight==20 && limits.resize.widthIncrement==8);
    assert(limits.resize.aspectBaseWidth==10 && limits.resize.maxAspectX==16);
    all[0]=LORIE_HINT_MIN_SIZE|LORIE_HINT_RESIZE_INC|LORIE_HINT_ASPECT;
    all[5]=30; all[6]=40;
    limits=lorieWindowSizeHints(all,15);
    assert(limits.resize.baseWidth==30 && limits.resize.aspectBaseWidth==0);
    all[9]=0; all[10]=UINT32_MAX; all[11]=UINT32_MAX;
    limits=lorieWindowSizeHints(all,18);
    assert(limits.resize.widthIncrement==1 && limits.resize.heightIncrement==1 && !limits.resize.minAspectX);
    for (size_t count=0;count<18;count++) {
        limits=lorieWindowSizeHints(all,count);
        assert(limits.minWidth>0 && limits.minWidth<=limits.maxWidth);
    }
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
