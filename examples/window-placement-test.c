#include <assert.h>
#include "../lorie/src/main/cpp/lorie/window_placement.h"
#include "../lorie/src/main/cpp/lorie/window_role.h"

int main(void) {
    assert(lorieCenterTransientAxis(545, 209, 1344) == 608);
    assert(lorieCenterTransientAxis(299, 46, 773) == 283);
    assert(lorieCenterTransientAxis(100, 300, 200) == 350);
    assert(lorieCenterTransientAxis(300, 50, 100) == 50);
    assert(lorieCenterTransientAxis(100, 50, 100) == 50);
    assert(lorieCenterTransientAxis(50, -10, 100) == 15);
    uint32_t hints[18] = {0};
    assert(!lorieWindowPositionSpecified(NULL, 18));
    assert(!lorieWindowPositionSpecified(hints, 0));
    assert(!lorieWindowPositionSpecified(hints, 18));
    hints[0] = (1u << 4) | (1u << 5) | (1u << 8) | (1u << 9);
    assert(!lorieWindowPositionSpecified(hints, 18));
    hints[0] |= 1u << 0;
    assert(lorieWindowPositionSpecified(hints, 18));
    hints[0] = 1u << 2;
    assert(lorieWindowPositionSpecified(hints, 18));
    assert(lorieWindowPositionSpecified(hints, 1));
    assert(loriePlaceTransientAxis(90, 30, 0, 100) == 70);
    assert(loriePlaceTransientAxis(-20, 30, 0, 100) == 0);
    assert(loriePlaceTransientAxis(25, 30, 0, 100) == 25);
    assert(loriePlaceTransientAxis(120, 150, 10, 100) == 10);
    assert(loriePlaceTransientAxis(-50, 150, 10, 100) == 10);
    assert(loriePlaceTransientAxis(70, 30, 0, 100) == 70);
    assert(lorieOutputAxis(10000, 25, 2000) == 2024);
    assert(lorieOutputAxis(5000, 25, 2001) == 1025);
    assert(lorieOutputAxis(-20, 25, 2001) == 25);
    assert(lorieOutputAxis(20000, 25, 2001) == 2025);
    assert(lorieOutputAxis(10000, 25, 0) == 25);
    assert(lorieWindowRole(1, 0, 1, 1, 1) == LORIE_WINDOW_SPLASH);
    assert(lorieWindowRole(0, 1, 1, 1, 1) == LORIE_WINDOW_DIALOG);
    assert(lorieWindowRole(0, 1, 0, 0, 0) == LORIE_WINDOW_DIALOG);
    assert(lorieWindowRole(0, 0, 0, 0, 0) == LORIE_WINDOW_UNCLASSIFIED);
    assert(lorieWindowRole(0, 0, 1, 0, 0) == LORIE_WINDOW_APPLICATION);
    assert(lorieWindowRole(0, 0, 0, 1, 0) == LORIE_WINDOW_APPLICATION);
    assert(lorieWindowRole(0, 0, 0, 0, 1) == LORIE_WINDOW_APPLICATION);
    return 0;
}
