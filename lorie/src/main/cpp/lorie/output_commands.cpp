#include "output_command.h"
#include <algorithm>
#include <cmath>

static void send(LorieConnection* connection, LorieOutputOperation operation, uint32_t output = 0,
        uint32_t window = 0, int32_t x = 0, int32_t y = 0, uint16_t detail = 0, bool down = false) {
    const LorieOutputCommand command{.t = 0, .operation = (uint8_t)operation, .down = (uint8_t)down,
            .output = output, .window = window, .x = x, .y = y, .detail = detail};
    lorieSendOutputCommand(connection, &command);
}

void lorieOutputBind(LorieConnection* c, uint32_t output, uint32_t window) {
    send(c, LORIE_OUTPUT_BIND, output, window);
}
void lorieOutputResize(LorieConnection* c, uint32_t output, uint32_t window, int width, int height) {
    send(c, LORIE_OUTPUT_RESIZE, output, window, width, height);
}
void lorieOutputPointer(LorieConnection* c, uint32_t output, uint32_t window, float x, float y, uint16_t button, bool down) {
    auto coordinate = [](float value) { return (int32_t)std::floor((double)(std::clamp(value, 0.f, 1.f) * 10000.f) + .5); };
    send(c, LORIE_OUTPUT_POINTER, output, window, coordinate(x), coordinate(y), button, down);
}
void lorieOutputKey(LorieConnection* c, uint32_t output, uint32_t window, uint16_t xKeyCode, bool down) {
    send(c, LORIE_OUTPUT_KEY, output, window, 0, 0, xKeyCode, down);
}
void lorieOutputText(LorieConnection* c, uint32_t output, uint32_t window, uint32_t codePoint) {
    send(c, LORIE_OUTPUT_TEXT, output, window, (int32_t)codePoint);
}
void lorieOutputFocus(LorieConnection* c, uint32_t output, uint32_t window) {
    send(c, LORIE_OUTPUT_FOCUS, output, window);
}
void lorieOutputRelease(LorieConnection* c, uint32_t output, uint32_t window) {
    send(c, LORIE_OUTPUT_RELEASE, output, window);
}
void lorieObserveWindows(LorieConnection* c) { send(c, LORIE_OUTPUT_OBSERVE); }
void lorieInspectWindow(LorieConnection* c, uint32_t serial, uint32_t window, uint16_t limit) {
    send(c, LORIE_OUTPUT_INSPECT, serial, window, 0, 0, limit);
}
void lorieCloseWindow(LorieConnection* c, uint32_t window) { send(c, LORIE_OUTPUT_CLOSE, 0, window); }
void lorieSetScreenDpi(LorieConnection* c, int dpi) { send(c, LORIE_OUTPUT_DPI, 0, 0, dpi); }
void lorieConfirmWindowState(LorieConnection* c, uint32_t window, uint32_t requestSerial, LorieWindowState actual) {
    send(c, LORIE_OUTPUT_FULLSCREEN_CONFIRM, 0, window, (int32_t)requestSerial, 0, 0, actual.fullscreen);
}
