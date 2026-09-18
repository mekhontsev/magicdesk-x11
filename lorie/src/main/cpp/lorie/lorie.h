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
void lorieSetCursorVisible(Bool visible);
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
    EVENT_INSPECTION_NODE,
    EVENT_INSPECTION_DONE,
    EVENT_DATA,
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
    struct { uint8_t t; uint32_t serial; LorieInspectionNode node; } inspectionNode;
    struct { uint8_t t; uint32_t serial; LorieInspectionResult result; } inspectionDone;
    struct {
        uint8_t t;
        uint32_t output, window, width, height;
        uint64_t bufferId, revision;
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
    } windowInfo;
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
struct _Pixmap;
LorieBuffer* lorieExportPixmap(struct _Pixmap* pixmap);
void lorieSendOutputFrame(const lorieEvent* event);
void lorieSendWindowInfo(const lorieEvent* event, const uint32_t* icon);

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
     * Root window and cursor content and properties are shared across these 2 processes.
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

    /*
     * We do not want to block the X server for an extended period; ideally, we would avoid blocking it at all.
     * However, if we don’t block the X server, it will overwrite root window memory fragment, causing tearing or frame distortion.
     * On some devices, there is no way to make EGL/GLES2 render a frame without calling eglSwapBuffers;
     * calls like glFinish, eglWaitGL, and eglWaitClient have no effect.
     * The only way to force EGL to render a frame and flush the command queue is by invoking eglSwapBuffers.
     * But eglSwapBuffers will not return until Android actually displays the frame.
     * Since we want to proceed as quickly as possible, waiting for the frame to be shown is not acceptable.
     *
     * Therefore, we set eglSwapInterval(dpy, 1), so that eglSwapBuffers does not block until the frame is displayed.
     * Even then, we do not want to waste GPU resources rendering more than one full-screen quad per vsync,
     * because that would spend GPU time on a frame that will never be shown.
     * To handle this, we use a waitForNextFrame flag, which we set after a successful render and clear from the AChoreographer’s frame callback.
     */
    volatile uint8_t waitForNextFrame;

    /* Needed to show FPS counter in logcat */
    volatile int renderedFrames;

    struct {
        // We should not allow updating cursor content the same time renderer draws it.
        // locking the mutex protecting the root window can cause waiting for the frame to be drawn which is unacceptable
        pthread_mutex_t lock; // initialized at X server side.
        uint32_t x, y, xhot, yhot, width, height;
        uint32_t bits[512*512]; // 1 megabyte should be enough for any cursor up to 512x512
        // Signals to renderer to update cursor's texture or its coordinates
        volatile uint8_t updated, moved, visible;
    } cursor;
};

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <media/NdkImageReader.h>
#include "list.h"

struct Renderer {
    struct Output {
        Output* next = nullptr;
        uint32_t id = 0;
        ANativeWindow *window = nullptr, *pending = nullptr;
        EGLSurface surface = EGL_NO_SURFACE;
        bool changed = false;
        bool released = false;
        lorieEvent frame{};
        lorieEvent layers[LORIE_MAX_FAMILY_LAYERS]{}, pendingLayers[LORIE_MAX_FAMILY_LAYERS]{};
        unsigned layerCount = 0, pendingCount = 0;
        uint64_t drawnRevision = 0;
    };
    Output* outputs = nullptr;
    bool setOutputSurface(uint32_t id, ANativeWindow* window, bool release);
    void setOutputFrame(const lorieEvent& event);
    bool outputSurfacesChanged() const;
    bool hasOutputSurface() const;
    bool outputsNeedDraw() const;
    void refreshOutputSurfaces();
    void invalidateOutputs();
    void drawOutputs();
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLSurface defaultSfc = EGL_NO_SURFACE, sfc = EGL_NO_SURFACE;
    EGLConfig cfg = nullptr;
    ANativeWindow *defaultWin = nullptr, *win = nullptr;
    AImageReader* defaultReader = nullptr;
    struct xorg_list addedBuffers{}, buffers{}, removedBuffers{};
    volatile int filtering = GL_NEAREST;

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
    // FBO used to blit deferred Present "copy" entries (see lorieTryScheduleGpuCopy) into the root texture.
    GLuint gpuCopyFbo = 0;

    GLuint g_texture_program = 0, gv_pos = 0, gv_coords = 0;
    GLuint g_texture_program_bgra = 0, gv_pos_bgra = 0, gv_coords_bgra = 0;

    EGLint configAttribs[13] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE
    };

    int gpuDoneFd = -1;
    bool debugEnabled = false;
    uint64_t dstSizeLogCount = 0, srcSizeLogCount = 0;

    bool init();
    void destroy();
    void* initThread();
    ANativeWindow* createDefaultWindow();
    void releaseGraphics();
    int getWakeupCondFd() const;
    void testCapabilities(int* legacy_drawing, int* gpu_present_disabled);
    void setSharedState(struct lorie_shared_server_state* newState);
    bool lockSharedState();
    void addBuffer(LorieBuffer* buf);
    void removeBuffer(uint64_t id);
    void removeAllBuffers();
    LorieBuffer* findBufferWithRetry(uint64_t id);
    uint64_t applyPendingGpuCopiesLocked();
    void applyPendingGpuCopies();
    bool shouldWait();
    void threadLoop();
    void bindTexture(GLuint id) const;
    void notifyGpuCopyDone() const;
    void drawRegion(GLuint id, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint8_t flip);
};
#endif
