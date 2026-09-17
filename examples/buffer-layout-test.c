#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/buffer_layout.h"

int main(void) {
    size_t bytes;
    assert(lorieBufferLayout(1280, 1280, 720, 0, &bytes) && bytes == 1280 * 720 * 4);
    assert(lorieBufferLayout(3, 8, 2, 4, &bytes) && bytes == 44);
    assert(!lorieBufferLayout(8, 3, 2, 0, &bytes));
    assert(!lorieBufferLayout(1, 1, 1, 1, &bytes));
    assert(!lorieBufferLayout(0, 1, 1, 0, &bytes));
    assert(!lorieBufferLayout(1, INT32_MAX, 1, 0, &bytes));
    assert(!lorieBufferLayout(1, 1, 1, UINT64_MAX, &bytes));
    assert(!lorieBufferLayout(1, 1, 2, INT64_MAX - 3, &bytes));
    assert(loriePresentSizeMatches(1280, 720, 1280, 720));
    assert(!loriePresentSizeMatches(1280, 720, 1280, 1280));
    assert(!loriePresentSizeMatches(1280, 720, 720, 720));
    puts("X11 buffer layout and rectangular Present validation passed");
}
