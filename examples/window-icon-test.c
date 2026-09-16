#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/window_icon.h"

int main(void) {
    uint32_t output[LORIE_WINDOW_ICON_PIXELS];
    assert(!lorieWindowIcon(NULL, 0, output));
    const uint32_t pixel[] = {1, 1, 0x80402010};
    assert(lorieWindowIcon(pixel, 3, output));
    for (size_t i = 0; i < LORIE_WINDOW_ICON_PIXELS; i++) assert(output[i] == pixel[2]);
    const uint32_t wide[] = {2, 1, 0xff112233, 0xff445566};
    assert(lorieWindowIcon(wide, 4, output));
    assert(output[0] == 0 && output[16 * 64] == wide[2]);
    assert(output[16 * 64 + 32] == wide[3] && output[48 * 64] == 0);
    const uint32_t zero[] = {0, 1};
    const uint32_t huge[] = {UINT32_MAX, UINT32_MAX};
    const uint32_t truncated[] = {2, 2, 0};
    assert(!lorieWindowIcon(zero, 2, output));
    assert(!lorieWindowIcon(huge, 2, output));
    assert(!lorieWindowIcon(truncated, 3, output));
    assert(!lorieWindowIcon(pixel, 1, output));
    assert(!lorieWindowIcon(pixel, 1024 * 1024 + 1, output));
    uint32_t sizes[2 + 128 * 128 + 2 + 64 * 64 + 3] = {128, 128};
    size_t small = 2 + 128 * 128;
    sizes[2] = 0xffaa0000;
    sizes[small] = sizes[small + 1] = 64;
    sizes[small + 2] = 0xff00aa00;
    sizes[small + 2 + 64 * 64] = sizes[small + 3 + 64 * 64] = 1;
    assert(lorieWindowIcon(sizes, sizeof(sizes) / sizeof(sizes[0]), output));
    assert(output[0] == 0xff00aa00);
    assert(!lorieWindowIcon(sizes, sizeof(sizes) / sizeof(sizes[0]) - 1, output));
    puts("window icon tests passed");
    return 0;
}
