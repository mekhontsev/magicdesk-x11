#include "../lorie/src/main/cpp/lorie/output_command.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static LorieOutputCommand commands[32];
static unsigned count;
static LorieConnection* owner = reinterpret_cast<LorieConnection*>(1);

void lorieSendOutputCommand(LorieConnection* connection, const LorieOutputCommand* command) {
    assert(connection == owner && count < 32);
    commands[count++] = *command;
}

int main() {
    lorieObserveWindows(owner);
    lorieSetScreenDpi(owner, 144);
    lorieOutputBind(owner, 7, 0xf0000001);
    lorieOutputResize(owner, 7, 0xf0000001, 1920, 1080);
    lorieOutputPointer(owner, 7, 0xf0000001, .25f, .75f, 0, false);
    lorieOutputPointer(owner, 7, 0xf0000001, -1, 2, 1, true);
    lorieOutputPointer(owner, 7, 0xf0000001, -1, 2, 1, false);
    lorieOutputKey(owner, 7, 0xf0000001, 38, true);
    lorieOutputKey(owner, 7, 0xf0000001, 38, false);
    lorieOutputText(owner, 7, 0xf0000001, 0x1f600);
    lorieOutputFocus(owner, 7, 0xf0000001);
    lorieConfirmWindowState(owner, 0xf0000001, 0xffffffff, {.fullscreen = true});
    lorieCloseWindow(owner, 0xf0000001, false);
    lorieOutputRelease(owner, 7, 0xf0000001);

    const int operations[] = {7, 9, 0, 1, 2, 2, 2, 3, 3, 6, 5, 10, 8, 4};
    assert(count == sizeof(operations) / sizeof(operations[0]));
    for (unsigned i = 0; i < count; ++i) {
        assert(commands[i].operation == operations[i]);
        assert(commands[i].t == 0); // The transport supplies the outer event type.
    }
    assert(commands[0].output == 0 && commands[0].window == 0);
    assert(commands[1].x == 144);
    for (unsigned i = 2; i <= 10; ++i) {
        assert(commands[i].output == 7 && commands[i].window == 0xf0000001);
    }
    assert(commands[3].x == 1920 && commands[3].y == 1080);
    assert(commands[4].x == 2500 && commands[4].y == 7500 && commands[4].detail == 0);
    assert(commands[5].x == 0 && commands[5].y == 10000 && commands[5].down && commands[5].detail == 1);
    assert(!commands[6].down && commands[6].detail == 1);
    assert(commands[7].detail == 38 && commands[7].down && !commands[8].down);
    assert(commands[9].x == 0x1f600);
    assert(commands[11].output == 0 && commands[11].window == 0xf0000001);
    assert((uint32_t)commands[11].x == 0xffffffff && commands[11].down);
    assert(commands[12].output == 0 && commands[12].window == 0xf0000001);

    // Keep Java Math.round's previous boundary, including just below half a wire unit.
    lorieOutputPointer(owner, 8, 0, std::nextafter(.00005f, 0.f), 1.f, 0, false);
    assert(commands[14].x == 0 && commands[14].y == 10000);
    lorieOutputPointer(owner, 8, 0, .00005f, 0, 0, false);
    assert(commands[15].x == 1);
    lorieConfirmWindowState(owner, 0xf0000001, 2, {.fullscreen = false});
    assert(commands[16].x == 2 && !commands[16].down);
    lorieInspectWindow(owner, 42, 0xf0000001, 256);
    assert(commands[17].operation == LORIE_OUTPUT_INSPECT && commands[17].output == 42);
    assert(commands[17].window == 0xf0000001 && commands[17].detail == 256);
    lorieCloseWindow(owner, 0xf0000001, true);
    assert(commands[18].operation == LORIE_OUTPUT_CLOSE && commands[18].window == 0xf0000001);
    assert(commands[18].down && !commands[12].down);
    puts("Semantic output commands preserve ordering, IDs, coordinates and state acknowledgements");
}
