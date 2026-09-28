#include <dix-config.h>
#include <math.h>
#include <string.h>
#include <inputstr.h>
#include <inpututils.h>
#include <scrnintstr.h>
#include <X11/extensions/XIproto.h>
#include <exevents.h>
#include <exglobals.h>
#include "lorie.h"
#include "direct_input.h"

extern DeviceIntPtr lorieTouch, loriePen, lorieEraser;
extern ScreenPtr pScreenPtr;

typedef struct { uint32_t output; uint16_t contact; ValuatorMask position; } Contact;
static Contact contacts[20];
typedef struct { uint32_t output; unsigned buttons; ValuatorMask position; } Tablet;
static Tablet tablets[2];

void lorieDirectInputReset(void) {
    memset(contacts, 0, sizeof(contacts));
    memset(tablets, 0, sizeof(tablets));
}

int lorieDirectInputPressed(uint32_t output) {
    if (!output) return FALSE;
    for (unsigned i = 0; i < 20; i++) if (contacts[i].output == output) return TRUE;
    for (unsigned i = 0; i < 2; i++) if (tablets[i].output == output && (tablets[i].buttons & 1)) return TRUE;
    return FALSE;
}

static void tabletRelease(unsigned tool) {
    Tablet* state = &tablets[tool];
    DeviceIntPtr device = tool ? lorieEraser : loriePen;
    if (state->output && device) {
        for (unsigned i = 0; i < 3; i++) if (state->buttons & (1u << i))
            QueuePointerEvents(device, ButtonRelease, i + 1, POINTER_RELATIVE, NULL);
        QueueProximityEvents(device, ProximityOut, &state->position);
    }
    memset(state, 0, sizeof(*state));
}

void lorieDirectInputRelease(uint32_t output) {
    for (unsigned i = 0; i < 20; i++) if (contacts[i].output == output && output) {
        if (lorieTouch) QueueTouchEvents(lorieTouch, XI_TouchEnd, i + 1, 0, &contacts[i].position);
        memset(&contacts[i], 0, sizeof(contacts[i]));
    }
    for (unsigned i = 0; i < 2; i++) if (tablets[i].output == output && output) tabletRelease(i);
}

void lorieDirectInput(uint32_t output, const LorieOutputCommand* event, int x, int y) {
    if (!output || !pScreenPtr || !pScreenPtr->width || !pScreenPtr->height
            || !isfinite(event->pressure) || event->pressure < 0 || event->pressure > 1) return;
    if (event->operation == LORIE_OUTPUT_TOUCH) {
        if (!lorieTouch || event->detail > 31 || event->phase > LORIE_TOUCH_END) return;
        unsigned slot = 20;
        for (unsigned i = 0; i < 20; i++)
            if (contacts[i].output == output && contacts[i].contact == event->detail) { slot = i; break; }
        if (event->phase == LORIE_TOUCH_BEGIN) {
            if (slot != 20) return;
            for (unsigned i = 0; i < 20; i++) if (!contacts[i].output) { slot = i; break; }
        }
        // Updates after cancellation cannot resurrect a contact or synthesize a click.
        if (slot == 20) return;
        Contact* state = &contacts[slot];
        state->output = output; state->contact = event->detail;
        valuator_mask_zero(&state->position);
        valuator_mask_set_double(&state->position, 0, (double)x * 65535 / pScreenPtr->width);
        valuator_mask_set_double(&state->position, 1, (double)y * 65535 / pScreenPtr->height);
        valuator_mask_set_double(&state->position, 2, event->pressure * 65535);
        int type = event->phase == LORIE_TOUCH_BEGIN ? XI_TouchBegin
                : event->phase == LORIE_TOUCH_END ? XI_TouchEnd : XI_TouchUpdate;
        QueueTouchEvents(lorieTouch, type, slot + 1, 0, &state->position);
        if (event->phase == LORIE_TOUCH_END) memset(state, 0, sizeof(*state));
        return;
    }
    if (event->operation != LORIE_OUTPUT_TABLET || (event->buttons & ~7u)
            || event->eraser > 1 || !isfinite(event->tiltX) || !isfinite(event->tiltY)) return;
    unsigned tool = event->eraser;
    Tablet* state = &tablets[tool];
    if (!event->proximity) { if (state->output == output) tabletRelease(tool); return; }
    if (!loriePen || !lorieEraser) lorieSetStylusEnabled(TRUE);
    DeviceIntPtr device = tool ? lorieEraser : loriePen;
    if (!device) return;
    // Pen/eraser changes and output transfers terminate the previous proximity and buttons.
    if (tablets[!tool].output) tabletRelease(!tool);
    if (state->output && state->output != output) tabletRelease(tool);
    Bool entering = !state->output;
    state->output = output;
    valuator_mask_zero(&state->position);
    valuator_mask_set_double(&state->position, 0, (double)x * 0x3FFFF / pScreenPtr->width);
    valuator_mask_set_double(&state->position, 1, (double)y * 0x3FFFF / pScreenPtr->height);
    valuator_mask_set_double(&state->position, 2, event->pressure * 65535);
    valuator_mask_set_double(&state->position, 3, fmax(-64, fmin(63, event->tiltX * 180 / M_PI)));
    valuator_mask_set_double(&state->position, 4, fmax(-64, fmin(63, event->tiltY * 180 / M_PI)));
    if (entering) QueueProximityEvents(device, ProximityIn, &state->position);
    QueuePointerEvents(device, MotionNotify, 0, POINTER_ABSOLUTE, &state->position);
    // Tip, first barrel, second barrel map to left, right, middle in X11.
    unsigned next = (event->buttons & 1) | ((event->buttons & 2) << 1) | ((event->buttons & 4) >> 1);
    for (unsigned i = 0; i < 3; i++) if ((state->buttons ^ next) & (1u << i))
        QueuePointerEvents(device, next & (1u << i) ? ButtonPress : ButtonRelease, i + 1, POINTER_RELATIVE, NULL);
    state->buttons = next;
}
