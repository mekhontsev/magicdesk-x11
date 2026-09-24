#pragma once

#include <unistd.h>

#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <android/choreographer.h>
#include <android/log.h>

#include <stdbool.h>
#include <X11/Xdefs.h>
#include <X11/keysymdef.h>
#include <screenint.h>
#include <errno.h>
#include <sys/socket.h>
#include "linux/input-event-codes.h"
#include "buffer.h"
#include "data_exchange.h"
#include "shared_lock.h"
#include "output_command.h"
#include "cursor_image.h"
#include "family_geometry.h"


#ifdef __cplusplus
extern "C" {
#endif

struct lorie_shared_server_state;

void lorieConfigureNotify(int width, int height, int framerate, size_t name_size, char* name);
void lorieSetStylusEnabled(Bool enabled);
void lorieSyncLockKeysState(uint8_t state);
void lorieWakeServer(void);
void lorieRecheckGpuCopies(void);
void lorieChoreographerFrameCallback(__unused long t, AChoreographer* d);
void lorieActivityConnected(void);
void lorieSendSharedServerState(int memfd);
void lorieRegisterBuffer(LorieBuffer* buffer);
void lorieUnregisterBuffer(LorieBuffer* buffer);
bool lorieConnectionAlive(void);
void lorieSetRendererWakeupCond(int fd);
void lorieSetGpuDoneFd(int fd);
struct _Cursor;
void lorieCursorSet(struct _Cursor* cursor);
void lorieCursorSelect(uint32_t output, uint32_t window);
void lorieCursorRelease(uint32_t output);
void lorieCursorPublish(void);
void lorieSendSyncReply(uint32_t serial);

__unused void rendererTestCapabilities(int* legacy_drawing, int* gpu_present_disabled);

void lorieServerLock(pthread_mutex_t* mutex);

typedef enum {
    EVENT_UNKNOWN __unused = 0,
    EVENT_SHARED_SERVER_STATE,
    EVENT_ADD_BUFFER,
    EVENT_REMOVE_BUFFER,
    EVENT_SCREEN_SIZE,
    EVENT_TOUCH,
    EVENT_MOUSE,
    EVENT_KEY,
    EVENT_STYLUS,
    EVENT_STYLUS_ENABLE,
    EVENT_UNICODE,
    EVENT_CLIPBOARD_ENABLE,
    EVENT_CLIPBOARD_ANNOUNCE,
    EVENT_CLIPBOARD_REQUEST,
    EVENT_CLIPBOARD_SEND,
    EVENT_WINDOW_FOCUS_CHANGED,
    EVENT_RENDERER_WAKEUP_COND,
    EVENT_GPU_DONE_FD,
    EVENT_LOCK_KEYS_STATE,
    EVENT_SYNC,
    EVENT_SYNC_REPLY,
    EVENT_OUTPUT_COMMAND,
    EVENT_OUTPUT_FRAME,
    EVENT_OUTPUT_LAYER,
    EVENT_OUTPUT_WINDOW,
    EVENT_OUTPUT_WINDOWS_DONE,
    EVENT_OUTPUT_SHELL,
    EVENT_SHELL_STATE,
    EVENT_INSPECTION_NODE,
    EVENT_INSPECTION_DONE,
    EVENT_DATA,
    EVENT_OUTPUT_CURSOR,
    EVENT_OUTPUT_FAMILY,
} eventType;

typedef union {
    uint8_t type;
    LorieDataEvent data;
    struct {
        uint8_t t;
        uint16_t width, height, framerate;
        size_t name_size;
        char *name;
    } screenSize;
    struct {
        uint8_t t;
        unsigned long id;
    } removeBuffer;
    struct {
        uint8_t t;
        uint16_t type, id, x, y;
    } touch;
    struct {
        uint8_t t;
        float x, y;
        uint8_t detail, down, relative;
    } mouse;
    struct {
        uint8_t t;
        uint16_t key;
        uint8_t state;
    } key;
    struct {
        uint8_t t;
        float x, y;
        uint16_t pressure;
        int8_t tilt_x, tilt_y;
        int16_t orientation;
        uint8_t buttons, eraser, mouse;
    } stylus;
    struct {
        uint8_t t, enable;
    } stylusEnable;
    struct {
        uint8_t t;
        uint32_t code;
    } unicode;
    struct {
        uint8_t t;
        uint8_t enable;
    } clipboardEnable;
    struct {
        uint8_t t;
        uint32_t count;
    } clipboardSend;
    struct {
        uint8_t t;
        uint8_t state; // bit0 = Caps Lock, bit1 = Num Lock, bit2 = Scroll Lock
    } lockKeysState;
    struct {
        uint8_t t;
        uint32_t serial;
    } sync;
    LorieOutputCommand output;
    struct { uint8_t t; uint32_t output, window; LorieCursorInfo info; } cursor;
    struct { uint8_t t; uint32_t serial; LorieInspectionNode node; } inspectionNode;
    struct { uint8_t t; uint32_t serial; LorieInspectionResult result; } inspectionDone;
    struct {
        uint8_t t;
        uint32_t output, window, width, height;
        uint64_t bufferId, revision;
        uint32_t presentation;
        LorieShellRect viewport;
    } frame;
    struct {
        uint8_t t, alpha;
        uint32_t output, window, width, height;
        int32_t x, y;
        uint64_t bufferId;
    } layer;
    struct {
        uint8_t t, removed, mapped, hasIcon;
        uint32_t window, fullscreenSerial;
        uint8_t hostManaged, fullscreenRequested, fullscreenActual, role;
        char title[256];
        char instance[128], className[128];
    } windowInfo;
    struct { uint8_t t; uint32_t owner, window; uint8_t removed, available; } shell;
} lorieEvent;

#define LORIE_MAX_FAMILY_LAYERS 256

void lorieDensityInit(void);
void lorieDensityReset(void);
void lorieSetDpi(int dpi);
void lorieOutputCommand(const lorieEvent* event);
void lorieEmbeddedServerReady(void);
void lorieOutputWindowDestroyed(XID id);
void lorieOutputGeometryChanged(void);
void loriePrepareOutputs(void);
void loriePublishOutputs(struct lorie_shared_server_state* state);
void lorieResetOutputs(void);
void lorieReleaseOutputInput(void);
void lorieReleaseOutputButton(uint32_t output, uint32_t window, int button);
struct _Pixmap;
LorieBuffer* lorieExportPixmap(struct _Pixmap* pixmap);
void lorieSendOutputFrame(const lorieEvent* event);
void lorieSendWindowInfo(const lorieEvent* event, const uint32_t* icon);
void lorieSendShellInfo(const lorieEvent* event, const LorieShellInfo* info);
void lorieSendFamilyGeometry(uint32_t output, const LorieFamilyGeometry* info);
void lorieShellConfigure(uint32_t owner, int width, int height);
void lorieShellRefresh(void);
void lorieShellReset(void);
void lorieShellGeometryChanged(void);
Bool lorieShellWindow(XID window);
void lorieSendCursor(const lorieEvent* event, const uint32_t* pixels);

typedef struct { int16_t x1, y1, x2, y2; } LorieGpuCopyRect;

#define LORIE_GPU_COPY_MAX_RECTS 16
#define LORIE_GPU_COPY_QUEUE_CAPACITY 8

typedef struct {
    uint64_t serial;
    uint64_t srcBufferId;
    uint64_t dstBufferId;
    int16_t xOff, yOff;
    uint16_t numRects;
    LorieGpuCopyRect rects[LORIE_GPU_COPY_MAX_RECTS];
} LorieGpuCopyEntry;

struct lorie_shared_server_state {
    /*
     * Renderer and X server are separated into 2 different processes.
     * Root window content and properties are shared across these 2 processes.
     * Reading/drawing root window in renderer the same time X server writes it can cause
     * tearing, texture garbling and other visual artifacts so we should block X server while we are drawing.
     */
    pthread_mutex_t lock; // initialized at X server side.

    /*
     * Single-producer (X server, present_execute_copy)/single-consumer (renderer) ring buffer
     * of deferred GPU copies to be applied to the root window texture before it is drawn to screen.
     * X server only ever advances writeIndex, renderer only ever advances readIndex and completedSerial.
     */
    struct {
        volatile uint32_t writeIndex;
        volatile uint32_t readIndex;
        volatile uint64_t completedSerial;
        LorieGpuCopyEntry entries[LORIE_GPU_COPY_QUEUE_CAPACITY];
    } gpuCopyQueue;

    /* ID of root window texture to be drawn. */
    uint64_t rootWindowTextureID;

    /* A signal to renderer to update root window texture content from shared fragment if needed */
    volatile uint8_t drawRequested;

    /* We should avoid triggering renderer if there is no output surface */
    volatile uint8_t surfaceAvailable;

    /* The pixel mutex covers composition through GPU completion, never Android
     * buffer acquisition/presentation. Choreographer clears waitForNextFrame
     * at vsync, so damage cannot render frames faster than they can be shown.
     */
    volatile uint8_t waitForNextFrame;

    /* Needed to show FPS counter in logcat */
    volatile int renderedFrames;

};

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include "graphics.h"
#include "list.h"

struct Renderer {
    struct Output {
        Output* next = nullptr;
        uint32_t id = 0;
        ANativeWindow *window = nullptr, *pending = nullptr;
        void* surface = nullptr;
        bool changed = false;
        bool released = false;
        lorieEvent frame{};
        lorieEvent layers[LORIE_MAX_FAMILY_LAYERS]{}, pendingLayers[LORIE_MAX_FAMILY_LAYERS]{};
        unsigned layerCount = 0, pendingCount = 0;
        uint64_t drawnRevision = 0;
        uint32_t presented = 0;
    };
    Output* outputs = nullptr;
    void (*presentationCallback)(void*, uint32_t, uint32_t, bool) = nullptr;
    void* presentationContext = nullptr;
    bool setOutputSurface(uint32_t id, ANativeWindow* window, bool release);
    void setOutputFrame(const lorieEvent& event);
    bool outputSurfacesChanged() const;
    bool hasOutputSurface() const;
    bool outputsNeedDraw() const;
    void refreshOutputSurfaces();
    void invalidateOutputs();
    void drawOutputs();
    const LorieGraphics* graphics = nullptr;
    void* graphicsDevice = nullptr;
    struct Draw {
        uint32_t id;
        void* surface;
        ANativeWindow* window;
        lorieEvent frame;
        unsigned layerCount;
        lorieEvent layers[LORIE_MAX_FAMILY_LAYERS];
    };
    Draw* draws = nullptr;
    size_t drawCapacity = 0;
    struct xorg_list addedBuffers{}, buffers{}, removedBuffers{};
    bool filtering = false;

    pthread_t thread = 0;
    bool initialized = false;
    volatile bool stopping = false;
    volatile bool stateChanged = false;
    struct lorie_shared_server_state* pendingState = nullptr;

    pthread_mutex_t stateLock{};
    // Shared with the X server so it can signal us directly. Only this thread ever waits on it, so stateLock
    // (the companion mutex) doesn't need to be shared too.
    pthread_cond_t* stateCond = nullptr;
    pthread_cond_t stateChangeFinishCond{};
    pthread_spinlock_t bufferLock{};
    int stateCondFd = -1;
    struct lorie_shared_server_state* state = nullptr;
    int peerFd = -1; // Borrowed from this connection until shared-state detachment is acknowledged.
    bool connectionFailed = false;
    bool copyWaitingForBuffer = false;

    int gpuDoneFd = -1;

    bool init();
    void destroy();
    void* initThread();
    void releaseGraphics();
    int getWakeupCondFd() const;
    void setSharedState(struct lorie_shared_server_state* newState);
    bool lockSharedState();
    void addBuffer(LorieBuffer* buf);
    void removeBuffer(uint64_t id);
    void removeAllBuffers();
    void attachBuffer(LorieBuffer* buffer);
    LorieBuffer* findBuffer(uint64_t id);
    uint64_t applyPendingGpuCopiesLocked();
    void applyPendingGpuCopies();
    bool shouldWait();
    void threadLoop();
    void notifyGpuCopyDone() const;
};
#endif
