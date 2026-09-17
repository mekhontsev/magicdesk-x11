#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <android/native_window.h>

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
    void (*frame)(void*, uint32_t output, uint32_t window, int width, int height, bool available);
    void (*disconnected)(void*);
    // Text and icon memory is borrowed only for the callback. Icons are 64x64 ARGB.
    void (*window)(void*, uint32_t id, const char* title, const uint32_t* icon, bool removed, bool mapped,
            bool hostManaged, uint32_t fullscreenSerial, bool fullscreenRequested, bool fullscreenActual);
    void (*windowsCommitted)(void*);
    // Receiver owns descriptor when nonnegative. Callbacks run on the connection's Looper.
    void (*data)(void*, int operation, int channel, uint32_t serial, uint32_t offer,
            uint32_t output, uint32_t window, int x, int y, const char* type, int descriptor);
} LorieCallbacks;

enum LorieCommand {
    LORIE_OUTPUT_BIND = 0, LORIE_OUTPUT_RESIZE = 1, LORIE_OUTPUT_POINTER = 2, LORIE_OUTPUT_KEY = 3,
    LORIE_OUTPUT_RELEASE = 4, LORIE_OUTPUT_FOCUS = 5, LORIE_OUTPUT_TEXT = 6,
    LORIE_OUTPUT_OBSERVE = 7, LORIE_OUTPUT_CLOSE = 8, LORIE_OUTPUT_DPI = 9,
    LORIE_OUTPUT_FULLSCREEN_CONFIRM = 10
};

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
void lorieConnectionCommand(LorieConnection* connection, uint32_t output, uint32_t window,
        int operation, int x, int y, int detail, bool down);
void lorieConnectionData(LorieConnection* connection, int operation, int channel,
        uint32_t serial, uint32_t offer, uint32_t output, uint32_t window,
        int x, int y, const char* type, int borrowedDescriptor);
void lorieConnectionDestroy(LorieConnection* connection);

#ifdef __cplusplus
}
#endif
