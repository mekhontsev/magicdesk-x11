#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "window_role.h"

typedef struct ANativeWindow ANativeWindow;

#ifdef __cplusplus
extern "C" {
#endif

// One server per process. Start on a prepared Android Looper; ready runs on the
// X server thread. Arguments are copied. Completion exits the isolated process.
bool lorieServerStart(int count, const char* const* arguments,
        void (*ready)(void* context, const char* display), void* context);
void lorieServerStop(void);
// Caller owns the returned socket. Replaces the previous renderer connection.
int lorieServerConnect(void);

typedef struct LorieConnection LorieConnection;
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
} LorieWindowManagement;
typedef struct {
    const char* title;
    const uint32_t* icon;
    bool mapped;
    LorieWindowRole role;
    LorieWindowManagement management;
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
} LorieCallbacks;

enum LorieDataCommand {
    LORIE_DATA_ENABLE = 1, LORIE_DATA_OFFER, LORIE_DATA_READ, LORIE_DATA_REQUEST,
    LORIE_DATA_REPLY, LORIE_DATA_ENTER, LORIE_DATA_MOVE, LORIE_DATA_LEAVE,
    LORIE_DATA_DROP, LORIE_DATA_STATUS, LORIE_DATA_FINISH, LORIE_DATA_CANCEL, LORIE_DATA_BEGIN
};
enum LorieDataChannel { LORIE_DATA_CLIPBOARD, LORIE_DATA_DRAG };

// Calls, including destroy, must be serialized on the creating Looper thread.
LorieConnection* lorieConnectionCreate(const LorieCallbacks* callbacks, void* context);
bool lorieConnectionConnect(LorieConnection* connection, int ownedDescriptor);
// Borrows window during the call; retains its own reference until acknowledged release.
bool lorieConnectionSurface(LorieConnection* connection, uint32_t output, ANativeWindow* window, bool release);
// All commands use the connection's one ordered, nonblocking queue.
void lorieOutputBind(LorieConnection*, uint32_t output, uint32_t window);
void lorieOutputResize(LorieConnection*, uint32_t output, uint32_t window, int width, int height);
// Finite coordinates normalized to content, excluding host letterboxing; clipped to [0,1]. Button 0 is motion.
void lorieOutputPointer(LorieConnection*, uint32_t output, uint32_t window, float x, float y, uint16_t button, bool down);
void lorieOutputKey(LorieConnection*, uint32_t output, uint32_t window, uint16_t xKeyCode, bool down);
void lorieOutputText(LorieConnection*, uint32_t output, uint32_t window, uint32_t codePoint);
void lorieOutputFocus(LorieConnection*, uint32_t output, uint32_t window);
void lorieOutputRelease(LorieConnection*, uint32_t output, uint32_t window);
void lorieObserveWindows(LorieConnection*);
void lorieCloseWindow(LorieConnection*, uint32_t window);
void lorieSetScreenDpi(LorieConnection*, int dpi);
void lorieConfirmWindowState(LorieConnection*, uint32_t window, uint32_t requestSerial, LorieWindowState actual);
void lorieConnectionData(LorieConnection* connection, int operation, int channel,
        uint32_t serial, uint32_t offer, uint32_t output, uint32_t window,
        int x, int y, const char* type, int borrowedDescriptor);
void lorieConnectionDestroy(LorieConnection* connection);

#ifdef __cplusplus
}
#endif
