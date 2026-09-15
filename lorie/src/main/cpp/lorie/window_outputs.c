#include <dix-config.h>
#include <stdlib.h>
#include <windowstr.h>
#include <dix.h>
#include <resource.h>
#include <damage.h>
#include <inputstr.h>
#include <inpututils.h>
#include <compint.h>
#include "lorie.h"

extern ScreenPtr pScreenPtr;
extern DeviceIntPtr lorieMouse, lorieKeyboard;
extern int ucs2keysym(long ucs);
extern void lorieKeysymKeyboardEvent(KeySym keysym, int down);

typedef struct OutputSelection {
    struct OutputSelection* next;
    uint32_t id;
    XID window;
    PixmapPtr pixmap;
    DamagePtr damage;
    Bool changed, seen, dead, redirected, resizePending;
    int width, height;
    uint64_t revision;
} OutputSelection;

static OutputSelection* selections;

static void damageDestroyed(__unused DamagePtr damage, void* closure) {
    OutputSelection* output = closure;
    output->pixmap = NULL;
    output->damage = NULL;
    output->changed = TRUE;
}

static void releaseSelection(OutputSelection* output) {
    WindowPtr window = NULL;
    if (output->damage) DamageDestroy(output->damage);
    Bool retained = FALSE;
    for (OutputSelection* other = selections; other; other = other->next)
        if (other != output && other->window == output->window && other->redirected) retained = TRUE;
    if (!retained && output->redirected && dixLookupWindow(&window, output->window,
            serverClient, DixWriteAccess) == Success)
        compUnredirectWindow(serverClient, window, CompositeRedirectAutomatic);
    free(output);
}

void lorieResetOutputs(void) {
    while (selections) {
        OutputSelection* output = selections;
        selections = output->next;
        releaseSelection(output);
    }
}

void lorieOutputCommand(const lorieEvent* event) {
    if (!pScreenPtr || !pScreenPtr->root || !event->output.output) return;
    OutputSelection** link = &selections;
    while (*link && (*link)->id != event->output.output) link = &(*link)->next;
    OutputSelection* output = *link;
    if (event->output.operation == LORIE_OUTPUT_RELEASE) {
        if (output) { *link = output->next; releaseSelection(output); }
        return;
    }
    if (event->output.operation == LORIE_OUTPUT_BIND) {
        if (output) { *link = output->next; releaseSelection(output); }
        output = calloc(1, sizeof(*output));
        if (!output) return;
        output->id = event->output.output;
        output->window = event->output.window;
        output->changed = TRUE;
        output->next = *link;
        *link = output;
        return;
    }
    if (!output || output->dead || output->window != event->output.window) return;
    if (event->output.operation == LORIE_OUTPUT_RESIZE) {
        if (event->output.x < 1 || event->output.y < 1 ||
                event->output.x > 16384 || event->output.y > 16384) return;
        output->width = event->output.x;
        output->height = event->output.y;
        output->resizePending = TRUE;
        return;
    }
    WindowPtr window = pScreenPtr->root;
    if (output->window && dixLookupWindow(&window, output->window,
            serverClient, DixWriteAccess) != Success) return;
    if (output->window && (event->output.operation == LORIE_OUTPUT_FOCUS ||
            event->output.operation == LORIE_OUTPUT_KEY || event->output.operation == LORIE_OUTPUT_TEXT || event->output.down)) {
        XID above = Above;
        ConfigureWindow(window, CWStackMode, &above, serverClient);
        SetInputFocus(serverClient, lorieKeyboard, output->window, RevertToParent, CurrentTime, FALSE);
    }
    if (event->output.operation == LORIE_OUTPUT_POINTER) {
        ValuatorMask mask;
        valuator_mask_zero(&mask);
        int x = max(0, min(event->output.x, 10000)) * (window->drawable.width - 1) / 10000;
        int y = max(0, min(event->output.y, 10000)) * (window->drawable.height - 1) / 10000;
        valuator_mask_set_double(&mask, 0, window->drawable.x + x);
        valuator_mask_set_double(&mask, 1, window->drawable.y + y);
        QueuePointerEvents(lorieMouse, MotionNotify, 0, POINTER_ABSOLUTE | POINTER_SCREEN | POINTER_NORAW, &mask);
        if (event->output.detail >= 1 && event->output.detail <= 7)
            QueuePointerEvents(lorieMouse, event->output.down ? ButtonPress : ButtonRelease,
                    event->output.detail, POINTER_RELATIVE, NULL);
    } else if (event->output.operation == LORIE_OUTPUT_KEY &&
            event->output.detail >= 8 && event->output.detail <= 255) {
        QueueKeyboardEvents(lorieKeyboard, event->output.down ? KeyPress : KeyRelease, event->output.detail);
    } else if (event->output.operation == LORIE_OUTPUT_TEXT) {
        int keysym = ucs2keysym(event->output.x);
        lorieKeysymKeyboardEvent(keysym, TRUE);
        lorieKeysymKeyboardEvent(keysym, FALSE);
    }
}

void loriePrepareOutputs(void) {
    if (!pScreenPtr || !pScreenPtr->root) return;
    for (OutputSelection* output = selections; output; output = output->next) {
        if (!output->resizePending || output->dead) continue;
        WindowPtr window = pScreenPtr->root;
        if (output->window && dixLookupWindow(&window, output->window,
                serverClient, DixWriteAccess) != Success) continue;
        if (!output->window) {
            lorieConfigureNotify(output->width, output->height, 60, 0, NULL);
        } else {
            int width = max(pScreenPtr->width, window->drawable.x + output->width);
            int height = max(pScreenPtr->height, window->drawable.y + output->height);
            if (width != pScreenPtr->width || height != pScreenPtr->height)
                lorieConfigureNotify(width, height, 60, 0, NULL);
            XID size[] = {(XID)output->width, (XID)output->height};
            ConfigureWindow(window, CWWidth | CWHeight, size, serverClient);
        }
        output->resizePending = FALSE;
    }
}

void loriePublishOutputs(struct lorie_shared_server_state* state) {
    for (OutputSelection* output = selections; output; output = output->next) {
        WindowPtr window = pScreenPtr->root;
        PixmapPtr pixmap = NULL;
        if (!output->dead && (!output->window || dixLookupWindow(&window,
                output->window, serverClient, DixReadAccess) == Success)) {
            output->seen = TRUE;
            if (output->window && !output->redirected) {
                for (OutputSelection* other = selections; other; other = other->next)
                    if (other != output && other->window == output->window && other->redirected) output->redirected = TRUE;
            }
            if (output->window && !output->redirected)
                output->redirected = compRedirectWindow(serverClient, window,
                        CompositeRedirectAutomatic) == Success;
            if (window->realized) pixmap = pScreenPtr->GetWindowPixmap(window);
        } else if (output->seen && !output->dead) {
            output->dead = TRUE;
            output->changed = TRUE;
        }
        if (output->window && pixmap == pScreenPtr->GetScreenPixmap(pScreenPtr)) pixmap = NULL;
        if (output->pixmap != pixmap) {
            if (output->damage) DamageDestroy(output->damage);
            output->pixmap = pixmap;
            output->changed = TRUE;
            if (pixmap) {
                output->damage = DamageCreate(NULL, damageDestroyed, DamageReportNone, TRUE, pScreenPtr, output);
                if (output->damage) DamageRegister(&pixmap->drawable, output->damage);
            }
        }
        if (!output->changed && (!output->damage || !RegionNotEmpty(DamageRegion(output->damage)))) continue;
        lorie_mutex_lock(&state->lock, &state->lockingPid);
        LorieBuffer* buffer = lorieExportPixmap(pixmap);
        lorieEvent event = {.frame = {.t = EVENT_OUTPUT_FRAME, .output = output->id,
                .window = output->window, .revision = ++output->revision}};
        if (buffer) {
            const LorieBuffer_Desc* desc = LorieBuffer_description(buffer);
            event.frame.bufferId = desc->id;
            event.frame.width = desc->width;
            event.frame.height = desc->height;
        }
        lorie_mutex_unlock(&state->lock, &state->lockingPid);
        lorieSendOutputFrame(&event);
        if (output->damage) DamageEmpty(output->damage);
        output->changed = FALSE;
    }
}
