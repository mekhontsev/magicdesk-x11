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
void lorieOutputBindShell(LorieConnection* c, uint32_t output, uint32_t window) {
    send(c, LORIE_OUTPUT_BIND, output, window, 0, 0, 1);
}
void lorieOutputBindDependents(LorieConnection* c, uint32_t output, uint32_t window, uint32_t parent) {
    send(c, LORIE_OUTPUT_BIND, output, window, (int32_t)parent, 0, 2);
}
void lorieConfigureShell(LorieConnection* c, uint32_t owner, int width, int height) {
    send(c, LORIE_OUTPUT_SHELL, owner, 0, width, height);
}
void loriePresentShell(LorieConnection* c, uint32_t output, uint32_t window, uint32_t serial, LorieShellRect viewport) {
    LorieOutputCommand command{};
    command.operation = LORIE_OUTPUT_VIEWPORT; command.output = output; command.window = window;
    command.serial = serial; command.viewport = viewport;
    lorieSendOutputCommand(c, &command);
}
void lorieOutputResize(LorieConnection* c, uint32_t output, uint32_t window, int width, int height) {
    send(c, LORIE_OUTPUT_RESIZE, output, window, width, height);
}
static int32_t coordinate(float value) {
    return (int32_t)std::floor((double)(std::clamp(value, 0.f, 1.f) * 10000.f) + .5);
}
void lorieOutputPointer(LorieConnection* c, uint32_t output, uint32_t window, float x, float y, uint16_t button, bool down) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    send(c, LORIE_OUTPUT_POINTER, output, window, coordinate(x), coordinate(y), button, down);
}
void lorieOutputScroll(LorieConnection* c, uint32_t output, uint32_t window, float x, float y, float horizontal, float vertical) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(horizontal) || !std::isfinite(vertical)) return;
    LorieOutputCommand command{};
    command.operation = LORIE_OUTPUT_SCROLL; command.output = output; command.window = window;
    command.x = coordinate(x); command.y = coordinate(y);
    command.horizontal = std::clamp(horizontal, -32.f, 32.f);
    command.vertical = std::clamp(vertical, -32.f, 32.f);
    if (command.horizontal || command.vertical) lorieSendOutputCommand(c, &command);
}
void lorieOutputKey(LorieConnection* c, uint32_t output, uint32_t window, uint16_t xKeyCode, bool down) {
    send(c, LORIE_OUTPUT_KEY, output, window, 0, 0, xKeyCode, down);
}
void lorieOutputTouch(LorieConnection* c, uint32_t output, uint32_t window, uint16_t contact,
        LorieTouchPhase phase, float x, float y, float pressure) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(pressure) || pressure < 0 || pressure > 1
            || contact > 31 || phase < LORIE_TOUCH_BEGIN || phase > LORIE_TOUCH_END) return;
    LorieOutputCommand command{};
    command.operation = LORIE_OUTPUT_TOUCH; command.output = output; command.window = window;
    command.x = coordinate(x); command.y = coordinate(y); command.pressure = pressure;
    command.detail = contact; command.phase = phase; command.down = phase == LORIE_TOUCH_BEGIN;
    lorieSendOutputCommand(c, &command);
}
void lorieOutputTablet(LorieConnection* c, uint32_t output, uint32_t window, bool eraser, bool proximity,
        float x, float y, float pressure, float tiltX, float tiltY, unsigned buttons) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(pressure) || pressure < 0 || pressure > 1
            || !std::isfinite(tiltX) || !std::isfinite(tiltY) || (buttons & ~7u)) return;
    LorieOutputCommand command{};
    command.operation = LORIE_OUTPUT_TABLET; command.output = output; command.window = window;
    command.x = coordinate(x); command.y = coordinate(y); command.pressure = pressure;
    command.tiltX = tiltX; command.tiltY = tiltY; command.buttons = buttons;
    command.eraser = eraser; command.proximity = proximity; command.down = buttons & 1;
    lorieSendOutputCommand(c, &command);
}
void lorieOutputCancelContacts(LorieConnection* c, uint32_t output, uint32_t window) {
    send(c, LORIE_OUTPUT_CANCEL_CONTACTS, output, window);
}
void lorieOutputText(LorieConnection* c, uint32_t output, uint32_t window, uint32_t codePoint) {
    send(c, LORIE_OUTPUT_TEXT, output, window, (int32_t)codePoint);
}
void lorieOutputFocus(LorieConnection* c, uint32_t output, uint32_t window) {
    send(c, LORIE_OUTPUT_FOCUS, output, window);
}
void lorieOutputBlur(LorieConnection* c, uint32_t output, uint32_t window) {
    send(c, LORIE_OUTPUT_BLUR, output, window);
}
void lorieOutputRelease(LorieConnection* c, uint32_t output, uint32_t window) {
    send(c, LORIE_OUTPUT_RELEASE, output, window);
}
void lorieObserveWindows(LorieConnection* c) { send(c, LORIE_OUTPUT_OBSERVE); }
void lorieInspectWindow(LorieConnection* c, uint32_t serial, uint32_t window, uint16_t limit) {
    send(c, LORIE_OUTPUT_INSPECT, serial, window, 0, 0, limit);
}
void lorieCloseWindow(LorieConnection* c, uint32_t window, bool force) {
    send(c, LORIE_OUTPUT_CLOSE, 0, window, 0, 0, 0, force);
}
void lorieSetScreenDpi(LorieConnection* c, int dpi) { send(c, LORIE_OUTPUT_DPI, 0, 0, dpi); }
void lorieSetScreenColorScheme(LorieConnection* c, int scheme) {
    if (scheme >= 0 && scheme <= 2) send(c, LORIE_OUTPUT_COLOR_SCHEME, 0, 0, scheme);
}
void lorieConfirmWindowState(LorieConnection* c, uint32_t window, uint32_t requestSerial, LorieWindowState actual) {
    send(c, LORIE_OUTPUT_FULLSCREEN_CONFIRM, 0, window, (int32_t)requestSerial, 0, 0, actual.fullscreen);
}

void lorieConfirmMaximized(LorieConnection* c, uint32_t window, uint32_t requestSerial, unsigned axes) {
    if (axes & ~3u) return;
    send(c, LORIE_OUTPUT_MAXIMIZED_CONFIRM, 0, window, (int32_t)requestSerial, (int32_t)axes);
}
void lorieConfirmInteraction(LorieConnection* c, uint32_t window, uint32_t requestSerial, unsigned flags) {
    if (flags & ~7u) return;
    send(c, LORIE_OUTPUT_INTERACTION_CONFIRM, 0, window, (int32_t)requestSerial, (int32_t)flags);
}
