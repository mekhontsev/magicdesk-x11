#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "window_role.h"
#include "window_inspection.h"
#include "cursor_image.h"
#include "shell_surface.h"
#include "family_geometry.h"
#include "graphics.h"
#include "maximized_state.h"
#include "window_size.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*ready)(void* context, const char* display);
    // Pure host layout policy, on the X server thread. Validated positive pixel
    // limits; width/height initially contain the retained Surface offer.
    void (*windowSize)(const LorieWindowConstraints*, int* width, int* height);
} LorieServerCallbacks;

// One server per process. Start on a prepared Android Looper; callbacks run on
// the X server thread. Arguments/callbacks are copied. Completion exits the process.
bool lorieServerStart(int count, const char* const* arguments,
        const LorieServerCallbacks* callbacks, void* context);
void lorieServerStop(void);
// Caller owns the returned socket. Replaces the previous renderer connection.
int lorieServerConnect(void);

typedef struct LorieConnection LorieConnection;
#include "window_interaction.h"
typedef struct {
    uint32_t serial;
    bool fullscreen;
} LorieWindowRequest;
typedef struct {
    bool fullscreen;
} LorieWindowState;
typedef struct {
    bool managed;
    LorieWindowRequest request;
    LorieWindowState actual;
    LorieMaximizedState maximized;
    LorieWindowInteraction interaction;
} LorieWindowManagement;
typedef struct {
    const char* title;
    const char* instance;
    const char* className;
    const uint32_t* icon;
    bool mapped;
    LorieWindowRole role;
    LorieWindowManagement management;
    uint32_t parent;
    int width, height;
    LorieWindowConstraints constraints;
} LorieWindowInfo;

typedef struct {
    void (*frame)(void*, uint32_t output, uint32_t window, int width, int height, bool available);
    void (*disconnected)(void*);
    // Null info removes the window. Snapshot/text/icon memory is borrowed for the callback; icons are 64x64 ARGB.
    void (*window)(void*, uint32_t id, const LorieWindowInfo* info);
    void (*windowsCommitted)(void*);
    // Receiver owns descriptor when nonnegative. Callbacks run on the connection's Looper.
    void (*data)(void*, int operation, int channel, uint32_t serial, uint32_t offer,
            uint32_t output, uint32_t window, int x, int y, const char* type, int descriptor);
    // One bounded read-only reply; borrowed nodes precede its completion on the connection Looper.
    void (*inspectionNode)(void*, uint32_t serial, const LorieInspectionNode*);
    void (*inspectionDone)(void*, uint32_t serial, const LorieInspectionResult*);
    // Shape changes for the pointer's output, never coordinates. Straight ARGB pixels are borrowed.
    void (*cursor)(void*, uint32_t output, uint32_t window, const LorieCursorInfo*, const uint32_t* pixels);
    void (*shell)(void*, uint32_t owner, uint32_t window, const LorieShellInfo*);
    void (*shellState)(void*, uint32_t owner, bool available);
    void (*family)(void*, uint32_t output, const LorieFamilyGeometry*);
    // Renderer-thread receipt, after the requested viewport has been submitted to Android.
    void (*presented)(void*, uint32_t output, uint32_t serial, bool success);
    // Client-decoration gestures, using EWMH directions 0..8 and 11 (cancel).
    void (*windowGesture)(void*, uint32_t window, unsigned direction);
} LorieCallbacks;

enum LorieDataCommand {
    LORIE_DATA_ENABLE = 1, LORIE_DATA_OFFER, LORIE_DATA_READ, LORIE_DATA_REQUEST,
    LORIE_DATA_REPLY, LORIE_DATA_ENTER, LORIE_DATA_MOVE, LORIE_DATA_LEAVE,
    LORIE_DATA_DROP, LORIE_DATA_STATUS, LORIE_DATA_FINISH, LORIE_DATA_CANCEL, LORIE_DATA_BEGIN
};
enum LorieDataChannel { LORIE_DATA_CLIPBOARD, LORIE_DATA_DRAG };

// Calls, including destroy, must be serialized on the creating Looper thread.
LorieConnection* lorieConnectionCreate(const LorieCallbacks* callbacks, void* context, const LorieGraphics* graphics);
bool lorieConnectionConnect(LorieConnection* connection, int ownedDescriptor);
// Borrows window during the call; retains its own reference until acknowledged release.
bool lorieConnectionSurface(LorieConnection* connection, uint32_t output, ANativeWindow* window, bool release);
// All commands use the connection's one ordered, nonblocking queue.
void lorieOutputBind(LorieConnection*, uint32_t output, uint32_t window);
void lorieOutputBindShell(LorieConnection*, uint32_t output, uint32_t window);
void lorieOutputBindDependents(LorieConnection*, uint32_t output, uint32_t window, uint32_t parent);
void lorieConfigureShell(LorieConnection*, uint32_t owner, int width, int height);
void loriePresentShell(LorieConnection*, uint32_t output, uint32_t window, uint32_t serial, LorieShellRect viewport);
void lorieOutputResize(LorieConnection*, uint32_t output, uint32_t window, int width, int height);
// Finite coordinates normalized to content, excluding host letterboxing; clipped to [0,1]. Button 0 is motion.
void lorieOutputPointer(LorieConnection*, uint32_t output, uint32_t window, float x, float y, uint16_t button, bool down);
// Fractional wheel units, positive right/down; the X server emulates core wheel buttons.
void lorieOutputScroll(LorieConnection*, uint32_t output, uint32_t window, float x, float y, float horizontal, float vertical);
typedef enum { LORIE_TOUCH_BEGIN, LORIE_TOUCH_UPDATE, LORIE_TOUCH_END } LorieTouchPhase;
void lorieOutputTouch(LorieConnection*, uint32_t output, uint32_t window, uint16_t contact,
        LorieTouchPhase phase, float x, float y, float pressure);
// Pressure [0,1], tilt in radians; buttons tip=1, primary barrel=2, secondary barrel=4.
void lorieOutputTablet(LorieConnection*, uint32_t output, uint32_t window, bool eraser, bool proximity,
        float x, float y, float pressure, float tiltX, float tiltY, unsigned buttons);
void lorieOutputCancelContacts(LorieConnection*, uint32_t output, uint32_t window);
void lorieOutputKey(LorieConnection*, uint32_t output, uint32_t window, uint16_t xKeyCode, bool down);
void lorieOutputText(LorieConnection*, uint32_t output, uint32_t window, uint32_t codePoint);
void lorieOutputFocus(LorieConnection*, uint32_t output, uint32_t window);
void lorieOutputBlur(LorieConnection*, uint32_t output, uint32_t window);
void lorieOutputRelease(LorieConnection*, uint32_t output, uint32_t window);
void lorieObserveWindows(LorieConnection*);
void lorieInspectWindow(LorieConnection*, uint32_t serial, uint32_t window, uint16_t limit);
// Graceful WM_DELETE_WINDOW, or disconnect the owning client when force is true.
void lorieCloseWindow(LorieConnection*, uint32_t window, bool force);
void lorieSetScreenDpi(LorieConnection*, int dpi);
// XSettings theme preference: 0 unspecified, 1 dark, 2 light. Does not replace a client settings manager.
void lorieSetScreenColorScheme(LorieConnection*, int scheme);
void lorieConfirmWindowState(LorieConnection*, uint32_t window, uint32_t requestSerial, LorieWindowState actual);
void lorieConfirmMaximized(LorieConnection*, uint32_t window, uint32_t requestSerial, unsigned axes);
void lorieConfirmInteraction(LorieConnection*, uint32_t window, uint32_t requestSerial, unsigned flags);
void lorieConnectionData(LorieConnection* connection, int operation, int channel,
        uint32_t serial, uint32_t offer, uint32_t output, uint32_t window,
        int x, int y, const char* type, int borrowedDescriptor);
void lorieConnectionDestroy(LorieConnection* connection);

#ifdef __cplusplus
}
#endif
