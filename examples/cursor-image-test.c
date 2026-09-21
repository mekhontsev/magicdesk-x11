#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/cursor_image.h"

int main(void) {
    LorieCursorInfo info = {LORIE_CURSOR_IMAGE, 512, 512, 511, 511};
    assert(lorieCursorValid(&info) && lorieCursorPixelCount(&info) == 512 * 512);
    info.width = 513;
    assert(!lorieCursorValid(&info) && !lorieCursorPixelCount(&info));
    info.width = 512; info.hotspotX = 512;
    assert(!lorieCursorValid(&info));
    info = (LorieCursorInfo){LORIE_CURSOR_IMAGE, 0, 1, 0, 0};
    assert(!lorieCursorValid(&info));
    info = (LorieCursorInfo){LORIE_CURSOR_HIDDEN, 0, 0, 0, 0};
    assert(lorieCursorValid(&info) && !lorieCursorPixelCount(&info));
    info.kind = LORIE_CURSOR_DEFAULT;
    assert(lorieCursorValid(&info));
    info.width = 1;
    assert(!lorieCursorValid(&info));
    info.kind = 99;
    assert(!lorieCursorValid(&info));
    assert(lorieCursorStraightArgb(0xff123456) == 0xff123456);
    assert(lorieCursorStraightArgb(0x80ff4020) == 0x80ff8040);
    assert(lorieCursorStraightArgb(0x00123456) == 0);
    uint8_t source[] = {0x81, 0}, mask[] = {0xc1, 0x80};
    assert(lorieCursorMonoPixel(source, mask, 0, 1, 0xff0000, 0x00ff00) == 0xffff0000);
    assert(lorieCursorMonoPixel(source, mask, 0, 0x40, 0xff0000, 0x00ff00) == 0xff00ff00);
    assert(lorieCursorMonoPixel(source, mask, 0, 2, 0xff0000, 0x00ff00) == 0);
    assert(lorieCursorMonoPixel(source, mask, 1, 0x80, 0xff0000, 0x00ff00) == 0xff00ff00);
    puts("Cursor bounds, hotspot, transparency, channel order and premultiplied alpha passed");
}
