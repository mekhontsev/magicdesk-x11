#include <dix-config.h>
#include <cursorstr.h>
#include <servermd.h>
#include "lorie.h"

// X-thread state only. Android draws the pointer; no renderer mutex or motion stream is needed.
static LorieCursorInfo cursorInfo;
static uint32_t cursorPixels[LORIE_CURSOR_SIZE_LIMIT * LORIE_CURSOR_SIZE_LIMIT];
static uint32_t cursorOutput, cursorWindow;
static Bool cursorPending;

void lorieCursorSet(CursorPtr cursor) {
    CursorBitsPtr bits = cursor ? cursor->bits : NULL;
    cursorInfo = (LorieCursorInfo){.kind = LORIE_CURSOR_HIDDEN};
    // Inherited server default belongs to the host. An explicitly created X cursor does not.
    if (cursor && cursor == rootCursor) cursorInfo.kind = LORIE_CURSOR_DEFAULT;
    else if (bits && bits->width && bits->height && !bits->emptyMask) {
        cursorInfo = (LorieCursorInfo){LORIE_CURSOR_IMAGE, bits->width, bits->height, bits->xhot, bits->yhot};
        if (!lorieCursorValid(&cursorInfo)) cursorInfo = (LorieCursorInfo){.kind = LORIE_CURSOR_DEFAULT};
        else if (bits->argb) {
            for (size_t i = 0; i < lorieCursorPixelCount(&cursorInfo); i++)
                cursorPixels[i] = lorieCursorStraightArgb(bits->argb[i]);
        } else {
            uint32_t foreground = ((cursor->foreRed & 0xff00) << 8) | (cursor->foreGreen & 0xff00) | (cursor->foreBlue >> 8);
            uint32_t background = ((cursor->backRed & 0xff00) << 8) | (cursor->backGreen & 0xff00) | (cursor->backBlue >> 8);
            size_t stride = BitmapBytePad(bits->width);
            for (unsigned y = 0; y < bits->height; y++) for (unsigned x = 0; x < bits->width; x++) {
                unsigned bit = BITMAP_BIT_ORDER == MSBFirst ? 0x80u >> (x & 7) : 1u << (x & 7);
                cursorPixels[y * bits->width + x] = lorieCursorMonoPixel(bits->source, bits->mask,
                        y * stride + x / 8, bit, foreground, background);
            }
        }
    }
    cursorPending = TRUE;
}

void lorieCursorSelect(uint32_t output, uint32_t window) {
    if (cursorOutput == output && cursorWindow == window) return;
    cursorOutput = output;
    cursorWindow = window;
    cursorPending = TRUE;
}

void lorieCursorRelease(uint32_t output) {
    if (cursorOutput == output) lorieCursorSelect(0, 0);
}

void lorieCursorPublish(void) {
    if (!cursorOutput || !cursorPending) return;
    cursorPending = FALSE;
    lorieEvent event = {.cursor = {.t = EVENT_OUTPUT_CURSOR, .output = cursorOutput,
            .window = cursorWindow, .info = cursorInfo}};
    lorieSendCursor(&event, cursorPixels);
}
