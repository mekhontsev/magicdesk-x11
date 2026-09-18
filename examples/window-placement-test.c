#include <assert.h>
#include "../lorie/src/main/cpp/lorie/window_placement.h"
#include "../lorie/src/main/cpp/lorie/window_role.h"

int main(void) {
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
    assert(lorieWindowRole(1, 1, 1, 1) == LORIE_WINDOW_SPLASH);
    assert(lorieWindowRole(0, 0, 0, 0) == LORIE_WINDOW_UNCLASSIFIED);
    assert(lorieWindowRole(0, 1, 0, 0) == LORIE_WINDOW_APPLICATION);
    assert(lorieWindowRole(0, 0, 1, 0) == LORIE_WINDOW_APPLICATION);
    assert(lorieWindowRole(0, 0, 0, 1) == LORIE_WINDOW_APPLICATION);
    return 0;
}
