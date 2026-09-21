#include <assert.h>
#include <stdio.h>
#include "../lorie/src/main/cpp/lorie/cursor.c"

static int publications;
CursorPtr rootCursor;
static lorieEvent last;
static uint32_t firstPixel;
void lorieSendCursor(const lorieEvent* event, const uint32_t* pixels) {
    last = *event;
    firstPixel = lorieCursorPixelCount(&event->cursor.info) ? pixels[0] : 0;
    publications++;
}

int main(void) {
    CARD32 pixels[] = {0x80ff4020};
    CursorBits bits = {.width = 1, .height = 1, .argb = pixels};
    CursorRec cursor = {.bits = &bits};
    lorieCursorSet(&cursor);
    lorieCursorPublish();
    assert(publications == 0);
    lorieCursorSelect(2, 30);
    lorieCursorPublish();
    assert(publications == 1 && last.cursor.output == 2 && last.cursor.window == 30);
    assert(firstPixel == 0x80ff8040 && last.cursor.info.kind == LORIE_CURSOR_IMAGE);
    for (int i = 0; i < 1000; i++) { lorieCursorSelect(2, 30); lorieCursorPublish(); }
    assert(publications == 1);
    lorieCursorSet(NULL);
    lorieCursorSet(&cursor);
    lorieCursorPublish();
    assert(publications == 2 && last.cursor.info.kind == LORIE_CURSOR_IMAGE);
    lorieCursorSelect(3, 30);
    lorieCursorPublish();
    assert(publications == 3 && last.cursor.output == 3);
    lorieCursorRelease(2);
    lorieCursorSet(NULL); lorieCursorPublish();
    assert(publications == 4 && last.cursor.info.kind == LORIE_CURSOR_HIDDEN);
    lorieCursorRelease(3);
    lorieCursorSet(&cursor); lorieCursorPublish();
    assert(publications == 4);
    lorieCursorSelect(4, 0); lorieCursorPublish();
    assert(publications == 5 && last.cursor.window == 0);
    CursorRec root = cursor;
    rootCursor = &root;
    lorieCursorSet(rootCursor); lorieCursorPublish();
    assert(publications == 6 && last.cursor.info.kind == LORIE_CURSOR_DEFAULT);
    // Identical pixels are not enough to classify a client cursor as the server default.
    lorieCursorSet(&cursor); lorieCursorPublish();
    assert(publications == 7 && last.cursor.info.kind == LORIE_CURSOR_IMAGE);
    lorieCursorSet(NULL); lorieCursorPublish();
    assert(publications == 8 && last.cursor.info.kind == LORIE_CURSOR_HIDDEN);
    bits.width = 513; lorieCursorSet(&cursor); lorieCursorPublish();
    assert(publications == 9 && last.cursor.info.kind == LORIE_CURSOR_DEFAULT);
    lorieCursorSelect(0, 0); lorieCursorPublish();
    assert(publications == 9);
    puts("Cursor output ownership, inherited default vs explicit shape, coalescing, stationary updates, hidden/reset and no motion publications passed");
}
