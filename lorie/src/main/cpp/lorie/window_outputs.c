#include <dix-config.h>
#include <stdlib.h>
#include <string.h>
#include <windowstr.h>
#include <dix.h>
#include <resource.h>
#include <damage.h>
#include <inputstr.h>
#include <inpututils.h>
#include <compint.h>
#include "lorie.h"
#include "window_model.h"

extern ScreenPtr pScreenPtr;
extern DeviceIntPtr lorieMouse, lorieKeyboard;
extern int ucs2keysym(long ucs);
extern void lorieKeysymKeyboardEvent(KeySym keysym, int down);

typedef struct WindowImage {
    struct WindowImage* next;
    XID window;
    PixmapPtr pixmap;
    DamagePtr damage;
    Bool changed, used, redirected;
} WindowImage;

#define MAX_FAMILY_LAYERS LORIE_MAX_FAMILY_LAYERS
typedef struct OutputSelection {
    struct OutputSelection* next;
    uint32_t id;
    XID window;
    Bool changed, seen, dead, resizePending, geometryPending, ownsSize;
    int width, height;
    unsigned layerCount;
    lorieEvent layers[MAX_FAMILY_LAYERS];
    uint64_t revision;
} OutputSelection;

static OutputSelection* selections;
static WindowImage* images;

Bool lorieOutputPoint(uint32_t id, uint32_t xid, int x, int y, WindowPtr* selected, int* rootX, int* rootY) {
    for (OutputSelection* output = selections; output; output = output->next) {
        if (output->id != id || output->window != xid || output->dead) continue;
        WindowPtr window = pScreenPtr->root;
        if (xid && dixLookupWindow(&window, xid, serverClient, DixReadAccess) != Success) return FALSE;
        *selected = window;
        *rootX = window->drawable.x + max(0, min(x, 10000)) * (window->drawable.width - 1) / 10000;
        *rootY = window->drawable.y + max(0, min(y, 10000)) * (window->drawable.height - 1) / 10000;
        return TRUE;
    }
    return FALSE;
}

void lorieOutputGeometryChanged(void) {
    for (OutputSelection* output = selections; output; output = output->next)
        if (output->window) output->geometryPending = TRUE;
}

void lorieOutputWindowDestroyed(XID id) {
    // Output ownership ends with the resource, even when its XID is immediately reused.
    for (OutputSelection* output = selections; output; output = output->next)
        if (output->window == id) { output->dead = TRUE; output->changed = TRUE; }
    for (WindowImage* image = images; image; image = image->next)
        if (image->window == id) image->redirected = FALSE;
}

static void damageDestroyed(__unused DamagePtr damage, void* closure) {
    WindowImage* output = closure;
    output->pixmap = NULL;
    output->damage = NULL;
    output->changed = TRUE;
}

static void releaseSelection(OutputSelection* output) {
    if (output->ownsSize) for (OutputSelection* other = selections; other; other = other->next) {
        if (other != output && !other->dead && other->window == output->window && other->width > 0) {
            other->ownsSize = TRUE;
            other->geometryPending = TRUE;
            break;
        }
    }
    free(output);
}

static void pruneImages(void) {
    WindowImage** link = &images;
    while (*link) {
        WindowImage* image = *link;
        if (image->used) {
            image->changed = FALSE;
            if (image->damage) DamageEmpty(image->damage);
            link = &image->next;
            continue;
        }
        *link = image->next;
        if (image->damage) DamageDestroy(image->damage);
        WindowPtr window = NULL;
        if (image->redirected && dixLookupWindow(&window, image->window, serverClient, DixWriteAccess) == Success)
            compUnredirectWindow(serverClient, window, CompositeRedirectAutomatic);
        free(image);
    }
}

void lorieResetOutputs(void) {
    lorieWindowModelReset();
    while (selections) {
        OutputSelection* output = selections;
        selections = output->next;
        releaseSelection(output);
    }
    for (WindowImage* image = images; image; image = image->next) image->used = FALSE;
    pruneImages();
}

void lorieOutputCommand(const lorieEvent* event) {
    if (!pScreenPtr || !pScreenPtr->root) return;
    if (event->output.operation == LORIE_OUTPUT_OBSERVE) { lorieWindowModelObserve(); return; }
    if (event->output.operation == LORIE_OUTPUT_DPI) { lorieSetDpi(event->output.x); return; }
    if (event->output.operation == LORIE_OUTPUT_CLOSE) { lorieWindowClose(event->output.window); return; }
    if (event->output.operation == LORIE_OUTPUT_FULLSCREEN_CONFIRM) {
        lorieWindowFullscreenConfirm(event->output.window, (uint32_t)event->output.x, event->output.down);
        return;
    }
    if (!event->output.output) return;
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
        output->geometryPending = TRUE;
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
        // Multiple views of one XID may differ in size. Only the latest resize
        // owns client geometry; passive views preserve aspect ratio.
        if (output->window) for (OutputSelection* other = selections; other; other = other->next)
            if (other->window == output->window) other->ownsSize = other == output;
        output->resizePending = TRUE;
        return;
    }
    WindowPtr window = pScreenPtr->root;
    if (output->window && dixLookupWindow(&window, output->window,
            serverClient, DixWriteAccess) != Success) return;
    if (output->window && (event->output.operation == LORIE_OUTPUT_FOCUS ||
            event->output.operation == LORIE_OUTPUT_KEY || event->output.operation == LORIE_OUTPUT_TEXT || event->output.down)) {
        lorieWindowFocus(window);
    }
    if (event->output.operation == LORIE_OUTPUT_POINTER) {
        lorieDataPointer(output->id, output->window, event->output.detail, event->output.down);
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
        if ((!output->resizePending && !output->geometryPending) || output->dead) continue;
        WindowPtr window = pScreenPtr->root;
        if (output->window && dixLookupWindow(&window, output->window,
                serverClient, DixWriteAccess) != Success) continue;
        if (!output->window) {
            if (output->resizePending) lorieConfigureNotify(output->width, output->height, 60, 0, NULL);
        } else {
            // An individual output is hosted by an Android window. Retain its last
            // Surface size when the client subsequently requests its saved geometry.
            int width = output->ownsSize ? output->width : window->drawable.width;
            int height = output->ownsSize ? output->height : window->drawable.height;
            int x = max(0, window->drawable.x), y = max(0, window->drawable.y);
            // Composite can expose off-screen pixels, but X input is clipped to the root.
            // Move the containing top-level (including any WM frame), not its child content.
            WindowPtr top = window;
            while (top->parent && top->parent != pScreenPtr->root) top = top->parent;
            if (x != window->drawable.x || y != window->drawable.y) {
                XID position[] = {
                    (XID)(top->origin.x - top->borderWidth + x - window->drawable.x),
                    (XID)(top->origin.y - top->borderWidth + y - window->drawable.y)
                };
                ConfigureWindow(top, CWX | CWY, position, serverClient);
            }
            int screenWidth = max(pScreenPtr->width, x + width);
            int screenHeight = max(pScreenPtr->height, y + height);
            if (screenWidth != pScreenPtr->width || screenHeight != pScreenPtr->height)
                lorieConfigureNotify(screenWidth, screenHeight, 60, 0, NULL);
            if (width != window->drawable.width || height != window->drawable.height) {
                XID size[] = {(XID)width, (XID)height};
                ConfigureWindow(window, CWWidth | CWHeight, size, serverClient);
            }
        }
        output->resizePending = FALSE;
        output->geometryPending = FALSE;
    }
}

static WindowImage* imageFor(WindowPtr window) {
    XID id = window == pScreenPtr->root ? 0 : window->drawable.id;
    WindowImage* image = images;
    while (image && image->window != id) image = image->next;
    if (!image) {
        image = calloc(1, sizeof(*image));
        if (!image) return NULL;
        image->window = id;
        image->changed = TRUE;
        image->next = images;
        images = image;
    }
    image->used = TRUE;
    if (id && !image->redirected)
        image->redirected = compRedirectWindow(serverClient, window, CompositeRedirectAutomatic) == Success;
    PixmapPtr pixmap = window->realized ? pScreenPtr->GetWindowPixmap(window) : NULL;
    if (id && pixmap == pScreenPtr->GetScreenPixmap(pScreenPtr)) pixmap = NULL;
    if (image->pixmap != pixmap) {
        if (image->damage) DamageDestroy(image->damage);
        image->pixmap = pixmap;
        image->changed = TRUE;
        if (pixmap) {
            image->damage = DamageCreate(NULL, damageDestroyed, DamageReportNone, TRUE, pScreenPtr, image);
            if (image->damage) DamageRegister(&pixmap->drawable, image->damage);
        }
    }
    return image;
}

typedef struct {
    WindowPtr owner;
    unsigned count;
    Bool changed;
    WindowImage* images[MAX_FAMILY_LAYERS];
    lorieEvent layers[MAX_FAMILY_LAYERS];
} FamilyFrame;

static void appendLayer(WindowPtr window, void* closure) {
    FamilyFrame* frame = closure;
    if (frame->count == MAX_FAMILY_LAYERS) return;
    WindowImage* image = imageFor(window);
    if (!image || !image->pixmap) return;
    unsigned index = frame->count++;
    frame->images[index] = image;
    frame->changed |= image->changed || (image->damage && RegionNotEmpty(DamageRegion(image->damage)));
    frame->layers[index].layer.t = EVENT_OUTPUT_LAYER;
    frame->layers[index].layer.window = window->drawable.id;
    frame->layers[index].layer.x = window->drawable.x - frame->owner->drawable.x;
    frame->layers[index].layer.y = window->drawable.y - frame->owner->drawable.y;
    frame->layers[index].layer.width = image->pixmap->drawable.width;
    frame->layers[index].layer.height = image->pixmap->drawable.height;
    frame->layers[index].layer.alpha = window->drawable.depth == 32;
}

void loriePublishOutputs(struct lorie_shared_server_state* state) {
    for (WindowImage* image = images; image; image = image->next) image->used = FALSE;
    for (OutputSelection* output = selections; output; output = output->next) {
        WindowPtr window = pScreenPtr->root;
        FamilyFrame frame = {0};
        if (!output->dead && (!output->window || dixLookupWindow(&window,
                output->window, serverClient, DixReadAccess) == Success)) {
            output->seen = TRUE;
            frame.owner = window;
            if (window->realized) {
                if (output->window) lorieWindowFamily(window, appendLayer, &frame);
                else appendLayer(window, &frame);
            }
        } else if (output->seen) output->dead = TRUE;
        for (unsigned i = 0; i < frame.count; i++) frame.layers[i].layer.output = output->id;
        if (!output->changed && !frame.changed && output->layerCount == frame.count
                && !memcmp(output->layers, frame.layers, frame.count * sizeof(lorieEvent))) continue;
        output->layerCount = frame.count;
        memcpy(output->layers, frame.layers, frame.count * sizeof(lorieEvent));
        lorieEvent event = {.frame = {.t = EVENT_OUTPUT_FRAME, .output = output->id,
                .window = output->window, .revision = ++output->revision}};
        LorieBuffer* buffers[MAX_FAMILY_LAYERS] = {0};
        lorieServerLock(&state->lock);
        for (unsigned i = 0; i < frame.count; i++)
            buffers[i] = lorieExportPixmap(frame.images[i]->pixmap);
        pthread_mutex_unlock(&state->lock);
        // IPC may apply backpressure. Never hold the pixel lock while publishing:
        // the receiver can be waiting for its renderer, which needs that lock.
        for (unsigned i = 0; i < frame.count; i++) {
            LorieBuffer* buffer = buffers[i];
            if (!buffer) continue;
            lorieRegisterBuffer(buffer);
            frame.layers[i].layer.bufferId = LorieBuffer_description(buffer)->id;
            lorieSendOutputFrame(&frame.layers[i]);
            if (frame.layers[i].layer.window == window->drawable.id) {
                event.frame.bufferId = frame.layers[i].layer.bufferId;
                event.frame.width = window->drawable.width;
                event.frame.height = window->drawable.height;
            }
        }
        // Commit the complete family atomically: no intermediate main-only frame.
        lorieSendOutputFrame(&event);
        output->changed = FALSE;
    }
    pruneImages();
}
