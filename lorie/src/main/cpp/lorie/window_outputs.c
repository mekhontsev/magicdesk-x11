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
#include "window_placement.h"
#include "window_size.h"

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
    int contentWidth, contentHeight;
    uint8_t keys[32], buttons;
    unsigned layerCount;
    lorieEvent layers[MAX_FAMILY_LAYERS];
    uint64_t revision;
} OutputSelection;

static OutputSelection* selections;
static WindowImage* images;

static void outputKey(OutputSelection* output, int key, Bool down) {
    uint8_t mask = 1u << (key % 8);
    if (!!(output->keys[key / 8] & mask) == !!down) return;
    if (down) output->keys[key / 8] |= mask;
    else output->keys[key / 8] &= ~mask;
    for (OutputSelection* other = selections; other; other = other->next)
        if (other != output && (other->keys[key / 8] & mask)) return;
    QueueKeyboardEvents(lorieKeyboard, down ? KeyPress : KeyRelease, key);
}

static void outputButton(OutputSelection* output, int button, Bool down) {
    uint8_t mask = 1u << button;
    if (!!(output->buttons & mask) == !!down) return;
    if (down) output->buttons |= mask;
    else output->buttons &= ~mask;
    for (OutputSelection* other = selections; other; other = other->next)
        if (other != output && (other->buttons & mask)) return;
    QueuePointerEvents(lorieMouse, down ? ButtonPress : ButtonRelease, button, POINTER_RELATIVE, NULL);
}

static void releaseInput(OutputSelection* output) {
    // A destroyed client cannot receive the host's later key/button-up. Release
    // this output's leases now, without releasing a key still held by another view.
    for (int key = 8; key <= 255; key++) outputKey(output, key, FALSE);
    for (int button = 1; button <= 7; button++) outputButton(output, button, FALSE);
}

void lorieReleaseOutputInput(void) {
    for (OutputSelection* output = selections; output; output = output->next) releaseInput(output);
}

Bool lorieOutputPoint(uint32_t id, uint32_t xid, int x, int y, WindowPtr* selected, int* rootX, int* rootY) {
    for (OutputSelection* output = selections; output; output = output->next) {
        if (output->id != id || output->window != xid || output->dead) continue;
        WindowPtr window = pScreenPtr->root;
        if (xid && dixLookupWindow(&window, xid, serverClient, DixReadAccess) != Success) return FALSE;
        *selected = window;
        *rootX = lorieOutputAxis(x, window->drawable.x, output->contentWidth);
        *rootY = lorieOutputAxis(y, window->drawable.y, output->contentHeight);
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
        if (output->window == id) {
            releaseInput(output);
            output->dead = TRUE;
            output->changed = TRUE;
        }
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
    releaseInput(output);
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
                event->output.x > LORIE_WINDOW_SIZE_LIMIT || event->output.y > LORIE_WINDOW_SIZE_LIMIT) return;
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
        int x, y;
        if (!lorieOutputPoint(output->id, output->window, event->output.x, event->output.y, &window, &x, &y)) return;
        valuator_mask_set_double(&mask, 0, x);
        valuator_mask_set_double(&mask, 1, y);
        QueuePointerEvents(lorieMouse, MotionNotify, 0, POINTER_ABSOLUTE | POINTER_SCREEN | POINTER_NORAW, &mask);
        if (event->output.detail >= 1 && event->output.detail <= 7)
            outputButton(output, event->output.detail, event->output.down);
    } else if (event->output.operation == LORIE_OUTPUT_KEY &&
            event->output.detail >= 8 && event->output.detail <= 255) {
        outputKey(output, event->output.detail, event->output.down);
    } else if (event->output.operation == LORIE_OUTPUT_TEXT) {
        int keysym = ucs2keysym(event->output.x);
        lorieKeysymKeyboardEvent(keysym, TRUE);
        lorieKeysymKeyboardEvent(keysym, FALSE);
    }
}

static void moveContent(WindowPtr window, int x, int y) {
    if (x == window->drawable.x && y == window->drawable.y) return;
    WindowPtr top = window;
    while (top->parent && top->parent != pScreenPtr->root) top = top->parent;
    XID position[] = {(XID)(top->origin.x - top->borderWidth + x - window->drawable.x),
            (XID)(top->origin.y - top->borderWidth + y - window->drawable.y)};
    ConfigureWindow(top, CWX | CWY, position, serverClient);
}

typedef struct {
    WindowPtr owner;
    int right, bottom;
    unsigned count;
} FamilyPlacement;

static void placeTransient(WindowPtr window, void* closure) {
    FamilyPlacement* placement = closure;
    if (placement->count++ >= MAX_FAMILY_LAYERS || window == placement->owner) return;
    WindowPtr owner = placement->owner;
    int x = loriePlaceTransientAxis(window->drawable.x, window->drawable.width,
            owner->drawable.x, owner->drawable.width);
    int y = loriePlaceTransientAxis(window->drawable.y, window->drawable.height,
            owner->drawable.y, owner->drawable.height);
    moveContent(window, x, y);
    placement->right = max(placement->right, x + window->drawable.width);
    placement->bottom = max(placement->bottom, y + window->drawable.height);
}

void loriePrepareOutputs(void) {
    if (!pScreenPtr || !pScreenPtr->root) return;
    for (OutputSelection* output = selections; output; output = output->next) {
        if ((!output->resizePending && !output->geometryPending) || output->dead) continue;
        Bool resize = output->resizePending;
        output->resizePending = output->geometryPending = FALSE;
        WindowPtr window = pScreenPtr->root;
        if (output->window && dixLookupWindow(&window, output->window,
                serverClient, DixWriteAccess) != Success) continue;
        if (!output->window) {
            if (resize) lorieConfigureNotify(output->width, output->height, 60, 0, NULL);
        } else {
            // The size owner proposes its Surface size; the X client keeps its
            // declared limits. Rendering and input aspect-fit the resulting canvas.
            int width = output->ownsSize ? output->width : window->drawable.width;
            int height = output->ownsSize ? output->height : window->drawable.height;
            if (output->ownsSize) lorieWindowConstrainSize(window, &width, &height);
            int x = max(0, window->drawable.x), y = max(0, window->drawable.y);
            // Composite can expose off-screen pixels, but X input is clipped to the root.
            // Move the containing top-level (including any WM frame), not its child content.
            moveContent(window, x, y);
            if (width != window->drawable.width || height != window->drawable.height) {
                XID size[] = {(XID)width, (XID)height};
                ConfigureWindow(window, CWWidth | CWHeight, size, serverClient);
            }
            FamilyPlacement placement = {.owner = window, .right = x + width, .bottom = y + height};
            lorieWindowFamily(window, placeTransient, &placement);
            int screenWidth = max(pScreenPtr->width, placement.right);
            int screenHeight = max(pScreenPtr->height, placement.bottom);
            if (screenWidth != pScreenPtr->width || screenHeight != pScreenPtr->height)
                lorieConfigureNotify(screenWidth, screenHeight, 60, 0, NULL);
        }
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
        if (frame.count) {
            event.frame.width = window->drawable.width;
            event.frame.height = window->drawable.height;
            for (unsigned i = 0; i < frame.count; i++) {
                event.frame.width = max((int)event.frame.width, frame.layers[i].layer.x + (int)frame.layers[i].layer.width);
                event.frame.height = max((int)event.frame.height, frame.layers[i].layer.y + (int)frame.layers[i].layer.height);
            }
        }
        output->contentWidth = event.frame.width;
        output->contentHeight = event.frame.height;
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
            }
        }
        // Commit the complete family atomically: no intermediate main-only frame.
        lorieSendOutputFrame(&event);
        output->changed = FALSE;
    }
    pruneImages();
}
