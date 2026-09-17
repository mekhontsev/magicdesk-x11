#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma ide diagnostic ignored "bugprone-reserved-identifier"
#pragma ide diagnostic ignored "OCUnusedMacroInspection"
#pragma ide diagnostic ignored "EndlessLoop"
#define __USE_GNU
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif
#include <android/log.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <libgen.h>
#include <cerrno>
#include <atomic>
extern "C" {
#include <globals.h>
#define class lorie_reserved_class
#define public lorie_reserved_public
#include <xkbsrv.h>
#include <inpututils.h>
#include <randrstr.h>
#include <mi.h>
#undef class
#undef public
}
#include <linux/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include "lorie.h"
#include "embedded.h"
#include "window_icon.h"
#include "gpu_completion.h"
#include "socket_io.h"

#define log(prio, ...) __android_log_print(ANDROID_LOG_ ## prio, "LorieNative", __VA_ARGS__)

static int argc = 0;
static char** argv = nullptr;
__LIBC_HIDDEN__ volatile int conn_fd = -1;
static std::atomic<uint64_t> dataConnection{0};
static int gpuDoneFd = -1;

// Registration, callbacks and teardown belong to the X server thread.
void lorieSetGpuDoneFd(int fd) {
    if (gpuDoneFd >= 0) {
        RemoveNotifyFd(gpuDoneFd);
        close(gpuDoneFd);
    }
    gpuDoneFd = fd;
    if (fd < 0) return;
    if (!SetNotifyFd(fd, +[](int eventFd, int ready, void*) {
            if (ready & X_NOTIFY_ERROR) { lorieSetGpuDoneFd(-1); return; }
            if (lorieConsumeGpuCompletion(eventFd)) lorieRecheckGpuCopies();
        }, X_NOTIFY_READ, nullptr)) {
        close(fd);
        gpuDoneFd = -1;
    }
}
extern DeviceIntPtr lorieMouse, lorieTouch, lorieKeyboard, loriePen, lorieEraser;
extern ScreenPtr pScreenPtr;
extern "C" int ucs2keysym(long ucs);
extern "C" void lorieKeysymKeyboardEvent(KeySym keysym, int down);

char *xtrans_unix_path_x11 = nullptr;
char *xtrans_unix_dir_x11 = nullptr;

struct xorg_list registeredBuffers;

static void (*serverReady)(void*, const char*);
static void* serverContext;
static lorieEvent incoming;
static size_t headerBytes;
static bool started;

bool lorieServerStart(int count, const char* const* arguments,
        void (*ready)(void*, const char*), void* context) {
    if (started || count < 0 || !ready) return false;
    const char* tmp = getenv("TMPDIR");
    const char* xkb = getenv("XKB_CONFIG_ROOT");
    if (!tmp || tmp[0] != '/' || access(tmp, W_OK | X_OK) ||
            !xkb || xkb[0] != '/' || access(xkb, R_OK | X_OK)) {
        log(ERROR, "X11 requires accessible TMPDIR and XKB_CONFIG_ROOT");
        return false;
    }
    argc = count + 1;
    argv = (char**)calloc(argc + 1, sizeof(char*));
    if (!argv) return false;
    argv[0] = strdup("Xlorie");
    for (int i = 0; i < count; ++i) argv[i + 1] = strdup(arguments[i]);
    for (int i = 0; i < argc; ++i) if (!argv[i]) {
        for (int j = 0; j < argc; ++j) free(argv[j]);
        free(argv);
        return false;
    }
    asprintf(&xtrans_unix_path_x11, "%s/.X11-unix/X", tmp);
    asprintf(&xtrans_unix_dir_x11, "%s/.X11-unix/", tmp);
    XkbBaseDirectory = xkb;
    serverReady = ready;
    serverContext = context;
    xorg_list_init(&registeredBuffers);
    pthread_t thread;
    started = true;
    int error = pthread_create(&thread, nullptr, +[](void*) -> void* {
        exit(dix_main(argc, argv, (char*[]) { nullptr }));
    }, nullptr);
    if (error) {
        started = false;
        for (int i = 0; i < argc; ++i) free(argv[i]);
        free(argv);
        free(xtrans_unix_path_x11);
        free(xtrans_unix_dir_x11);
        return false;
    }
    pthread_detach(thread);
    AChoreographer* choreographer = AChoreographer_getInstance();
    AChoreographer_postFrameCallback(choreographer, (AChoreographer_frameCallback)lorieChoreographerFrameCallback, choreographer);
    return true;
}

static Bool handleTouchEvent(__unused ClientPtr pClient, void *closure) {
    ValuatorMask mask;
    auto *e = (lorieEvent*) closure;
    double x = max(min((float) e->touch.x, pScreenPtr->width), 0);
    double y = max(min((float) e->touch.y, pScreenPtr->height), 0);
    valuator_mask_zero(&mask);
    DDXTouchPointInfoPtr touch = TouchFindByDDXID(lorieTouch, e->touch.id, FALSE);

    // Avoid duplicating events
    if (touch && touch->active) {
        double oldx = 0, oldy = 0;
        if (e->touch.type == XI_TouchUpdate &&
            valuator_mask_fetch_double(touch->valuators, 0, &oldx) &&
            valuator_mask_fetch_double(touch->valuators, 1, &oldy) &&
            oldx == x && oldy == y)
            goto end;
    }

    // Sometimes activity part does not send XI_TouchBegin and sends only XI_TouchUpdate.
    if (e->touch.type == XI_TouchUpdate && (!touch || !touch->active))
        e->touch.type = XI_TouchBegin;

    if (e->touch.type == XI_TouchEnd && (!touch || !touch->active))
        goto end;

    valuator_mask_set_double(&mask, 0, x * 0xFFFF / (float) pScreenPtr->width);
    valuator_mask_set_double(&mask, 1, y * 0xFFFF / (float) pScreenPtr->height);
    QueueTouchEvents(lorieTouch, e->touch.type, e->touch.id, 0, &mask);
    lorieSetCursorVisible(FALSE);

    end:
    free(e);
    return TRUE;
}

void handleLorieEvents(int fd, __unused int ready, __unused void *ignored) {
    ValuatorMask mask;
    lorieEvent e = {0};
    valuator_mask_zero(&mask);

    if (ready & X_NOTIFY_ERROR) {
        headerBytes = 0;
        InputThreadUnregisterDev(fd);
        close(fd);
        conn_fd = -1;
        uint64_t generation = ++dataConnection;
        QueueWorkProc(+[](__unused ClientPtr client, void* closure) -> Bool {
            if (dataConnection.load() == (uint64_t)(uintptr_t)closure) {
                lorieSetGpuDoneFd(-1);
                lorieDataReset();
                LorieBuffer* buf;
                while ((buf = LorieBufferList_first(&registeredBuffers))) LorieBuffer_removeFromList(buf);
            }
            return TRUE;
        }, nullptr, (void*)(uintptr_t)generation);
        lorieWakeServer();
        return;
    }

    again:
    ssize_t count = recv(fd, (char*)&incoming + headerBytes, sizeof(incoming) - headerBytes, MSG_DONTWAIT);
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) return;
    if (count <= 0) { handleLorieEvents(fd, X_NOTIFY_ERROR, nullptr); return; }
    headerBytes += count;
    if (headerBytes == sizeof(incoming)) {
        e = incoming;
        headerBytes = 0;
        switch(e.type) {
            case EVENT_OUTPUT_COMMAND: {
                auto* copy = (lorieEvent*) malloc(sizeof(e));
                if (!copy) break;
                *copy = e;
                QueueWorkProc(+[](__unused ClientPtr client, void* closure) -> Bool {
                    lorieOutputCommand((lorieEvent*) closure);
                    free(closure);
                    return TRUE;
                }, nullptr, copy);
                lorieWakeServer();
                break;
            }
            case EVENT_SCREEN_SIZE: {
                auto *copy = (lorieEvent*) calloc(1, sizeof(lorieEvent) + e.screenSize.name_size + 1);
                memcpy(copy, &e, sizeof(e));
                copy->screenSize.name = copy->screenSize.name_size ? (char*) (copy + 1) : nullptr;
                if (copy->screenSize.name_size)
                    read(fd, copy->screenSize.name, copy->screenSize.name_size);
                QueueWorkProc(+[](__unused ClientPtr pClient, void *closure) -> Bool {
                    // This must be done only on X server thread.
                    auto* e = (lorieEvent*) closure;
                    __android_log_print(ANDROID_LOG_ERROR, "tx11-request", "window changed: %d %d %s", e->screenSize.width, e->screenSize.height, e->screenSize.name);
                    lorieConfigureNotify(e->screenSize.width, e->screenSize.height, e->screenSize.framerate, e->screenSize.name_size, e->screenSize.name);
                    free(e);
                    return TRUE;
                }, nullptr, copy);
                lorieWakeServer();
                break;
            }
            case EVENT_TOUCH: {
                auto *copy = (lorieEvent*) calloc(1, sizeof(lorieEvent));
                memcpy(copy, &e, sizeof(e));
                QueueWorkProc(handleTouchEvent, nullptr, copy);
                lorieWakeServer();
                break;
            }
            case EVENT_STYLUS: {
                static int buttons_prev = 0;
                uint32_t released, pressed, diff;
                DeviceIntPtr device = e.stylus.mouse ? lorieMouse : (e.stylus.eraser ? lorieEraser : loriePen);
                if (!device) {
                    __android_log_print(ANDROID_LOG_DEBUG, "LorieNative", "got stylus event but device is not requested\n");
                    break;
                }
                __android_log_print(ANDROID_LOG_DEBUG, "LorieNative", "got stylus event %f %f %d %d %d %d %s\n", e.stylus.x, e.stylus.y, e.stylus.pressure, e.stylus.tilt_x, e.stylus.tilt_y, e.stylus.orientation,
                                    device == lorieMouse ? "lorieMouse" : (device == loriePen ? "loriePen" : "lorieEraser"));

                valuator_mask_set_double(&mask, 0, max(min(e.stylus.x, pScreenPtr->width), 0));
                valuator_mask_set_double(&mask, 1, max(min(e.stylus.y, pScreenPtr->height), 0));
                if (device != lorieMouse) {
                    valuator_mask_set_double(&mask, 2, e.stylus.pressure);
                    valuator_mask_set_double(&mask, 3, e.stylus.tilt_x);
                    valuator_mask_set_double(&mask, 4, e.stylus.tilt_y);
                    valuator_mask_set_double(&mask, 5, e.stylus.orientation);
                }
                QueuePointerEvents(device, MotionNotify, 0, POINTER_ABSOLUTE | POINTER_DESKTOP | (device == lorieMouse ? POINTER_NORAW : 0), &mask);

                diff = buttons_prev ^ e.stylus.buttons;
                released = diff & ~e.stylus.buttons;
                pressed = diff & e.stylus.buttons;

                for (int i=0; i<3; i++) {
                    if (released & 0x1) {
                        QueuePointerEvents(device, ButtonRelease, i + 1, POINTER_RELATIVE, nullptr);
                        __android_log_print(ANDROID_LOG_DEBUG, "LorieNative", "sending %d press", i+1);
                    }
                    if (pressed & 0x1) {
                        QueuePointerEvents(device, ButtonPress, i + 1, POINTER_RELATIVE, nullptr);
                        __android_log_print(ANDROID_LOG_DEBUG, "LorieNative", "sending %d release", i+1);
                    }
                    released >>= 1;
                    pressed >>= 1;
                }
                buttons_prev = e.stylus.buttons;

                break;
            }
            case EVENT_STYLUS_ENABLE: {
                lorieSetStylusEnabled(e.stylusEnable.enable);
                break;
            }
            case EVENT_MOUSE: {
                int flags;
                lorieSetCursorVisible(TRUE);
                switch(e.mouse.detail) {
                    case 0: // BUTTON_UNDEFINED
                        flags = (e.mouse.relative) ? POINTER_RELATIVE | POINTER_ACCELERATE : POINTER_ABSOLUTE | POINTER_SCREEN | POINTER_NORAW;
                        if (!e.mouse.relative) {
                            e.mouse.x = max(0, min(e.mouse.x, pScreenPtr->width));
                            e.mouse.y = max(0, min(e.mouse.y, pScreenPtr->height));
                        }
                        valuator_mask_set_double(&mask, 0, (double) e.mouse.x);
                        valuator_mask_set_double(&mask, 1, (double) e.mouse.y);
                        QueuePointerEvents(lorieMouse, MotionNotify, 0, flags, &mask);
                        break;
                    case 1: // BUTTON_LEFT
                    case 2: // BUTTON_MIDDLE
                    case 3: // BUTTON_RIGHT
                        QueuePointerEvents(lorieMouse, e.mouse.down ? ButtonPress : ButtonRelease, e.mouse.detail, POINTER_RELATIVE, nullptr);
                        break;
                    case 4: // BUTTON_SCROLL
                        if (e.mouse.x != 0.0f) {
                            valuator_mask_zero(&mask);
                            valuator_mask_set_double(&mask, 2, (double) e.mouse.x / 120);
                            QueuePointerEvents(lorieMouse, MotionNotify, 0, POINTER_RELATIVE, &mask);
                        }
                        if (e.mouse.y != 0.0f) {
                            valuator_mask_zero(&mask);
                            valuator_mask_set_double(&mask, 3, (double) e.mouse.y / 120);
                            QueuePointerEvents(lorieMouse, MotionNotify, 0, POINTER_RELATIVE, &mask);
                        }
                        break;
                }
                break;
            }
            case EVENT_KEY:
                QueueKeyboardEvents(lorieKeyboard, e.key.state ? KeyPress : KeyRelease, e.key.key);
                break;
            case EVENT_UNICODE: {
                int ks = ucs2keysym((long) e.unicode.code);
                __android_log_print(ANDROID_LOG_DEBUG, "LorieNative", "Trying to input keysym %d\n", ks);
                lorieKeysymKeyboardEvent(ks, TRUE);
                lorieKeysymKeyboardEvent(ks, FALSE);
                break;
            }
            case EVENT_DATA: {
                struct Command { LorieDataEvent event; int fd; uint64_t generation; };
                int descriptor = e.data.hasFd ? ancil_recv_fd(fd) : -1;
                if (e.data.hasFd && descriptor < 0) { handleLorieEvents(fd, X_NOTIFY_ERROR, nullptr); return; }
                auto* command = (Command*)calloc(1, sizeof(Command));
                if (!command) { if (descriptor >= 0) close(descriptor); handleLorieEvents(fd, X_NOTIFY_ERROR, nullptr); return; }
                command->event = e.data;
                command->fd = descriptor;
                command->generation = dataConnection.load();
                if (!QueueWorkProc(+[](__unused ClientPtr client, void* closure) -> Bool {
                    auto* command = (Command*)closure;
                    if (command->generation == dataConnection.load()) lorieDataCommand(&command->event, command->fd);
                    else if (command->fd >= 0) close(command->fd);
                    free(command);
                    return TRUE;
                }, nullptr, command)) {
                    if (descriptor >= 0) close(descriptor);
                    free(command);
                    handleLorieEvents(fd, X_NOTIFY_ERROR, nullptr);
                    return;
                }
                lorieWakeServer();
                break;
            }
            case EVENT_RENDERER_WAKEUP_COND: {
                int wakeupFd = ancil_recv_fd(fd);
                if (wakeupFd >= 0)
                    lorieSetRendererWakeupCond(wakeupFd);
                break;
            }
            case EVENT_GPU_DONE_FD: {
                struct Registration { int fd; uint64_t generation; };
                int descriptor = ancil_recv_fd(fd);
                if (descriptor < 0) { handleLorieEvents(fd, X_NOTIFY_ERROR, nullptr); return; }
                auto* request = (Registration*)malloc(sizeof(Registration));
                if (!request) { close(descriptor); handleLorieEvents(fd, X_NOTIFY_ERROR, nullptr); return; }
                *request = {descriptor, dataConnection.load()};
                if (!QueueWorkProc(+[](__unused ClientPtr, void *closure) -> Bool {
                    auto* request = (Registration*)closure;
                    if (request->generation == dataConnection.load()) lorieSetGpuDoneFd(request->fd);
                    else close(request->fd);
                    free(request);
                    return TRUE;
                }, nullptr, request)) {
                    close(descriptor);
                    free(request);
                    handleLorieEvents(fd, X_NOTIFY_ERROR, nullptr);
                    return;
                }
                lorieWakeServer();
                break;
            }
            case EVENT_SYNC: {
                auto serial = (uintptr_t) e.sync.serial;
                QueueWorkProc(+[](__unused ClientPtr pClient, void *closure) -> Bool {
                    // This must be done only on X server thread. Forces mieq to drain everything
                    // enqueued before this marker, so the reply is a reliable "the server has
                    // applied every event sent so far" barrier.
                    mieqProcessInputEvents();
                    lorieSendSyncReply((uint32_t) (uintptr_t) closure);
                    return TRUE;
                }, nullptr, (void*) serial);
                lorieWakeServer();
                break;
            }
            case EVENT_LOCK_KEYS_STATE: {
                auto *copy = (lorieEvent*) calloc(1, sizeof(lorieEvent));
                memcpy(copy, &e, sizeof(e));
                QueueWorkProc(+[](__unused ClientPtr pClient, void *closure) -> Bool {
                    // This must be done only on X server thread (touches XKB state directly).
                    auto *e = (lorieEvent*) closure;
                    lorieSyncLockKeysState(e->lockKeysState.state);
                    free(e);
                    return TRUE;
                }, nullptr, copy);
                lorieWakeServer();
                break;
            }
        }

        int n;
        if (ioctl(fd, FIONREAD, &n) >= 0 && n > sizeof(e))
            goto again;
    }
}

static bool sendData(const void* data, size_t size) {
    if (conn_fd < 0) return false;
    if (lorieWriteFully(conn_fd, data, size)) return true;
    shutdown(conn_fd, SHUT_RDWR);
    return false;
}

void lorieSendDataEvent(const LorieDataEvent* data, int descriptor) {
    if (conn_fd < 0) return;
    lorieEvent event{};
    event.data = *data;
    event.data.t = EVENT_DATA;
    event.data.hasFd = descriptor >= 0;
    if (sendData(&event, sizeof(event)) && descriptor >= 0 && ancil_send_fd(conn_fd, descriptor) < 0)
        shutdown(conn_fd, SHUT_RDWR);
}

void lorieSendSyncReply(uint32_t serial) {
    if (conn_fd != -1) {
        lorieEvent e = { .sync = { .t = EVENT_SYNC_REPLY, .serial = serial } };
        write(conn_fd, &e, sizeof(e));
    }
}

bool lorieConnectionAlive(void) {
    if (conn_fd == -1)
        return false;

    // Check if socket is closed or has errors.
    struct pollfd p = { .fd = conn_fd, .events = POLLIN | POLLHUP | POLLERR | POLLRDHUP };
    return !(poll(&p, 1, 0) == 1 && (p.revents & (POLLERR | POLLNVAL | POLLRDHUP | POLLHUP)));
}

void lorieSendSharedServerState(int memfd) {
    if (conn_fd != -1) {
        lorieEvent e = { .type = EVENT_SHARED_SERVER_STATE };
        if (sendData(&e, sizeof(e)) && ancil_send_fd(conn_fd, memfd) < 0)
            shutdown(conn_fd, SHUT_RDWR);
    }
}

void lorieSendOutputFrame(const lorieEvent* event) {
    sendData(event, sizeof(*event));
}

void lorieSendWindowInfo(const lorieEvent* event, const uint32_t* icon) {
    if (sendData(event, sizeof(*event)) && event->windowInfo.hasIcon)
        sendData(icon, LORIE_WINDOW_ICON_PIXELS * sizeof(*icon));
}

void lorieRegisterBuffer(LorieBuffer* buffer) {
    unsigned long id = LorieBuffer_description(buffer)->id;
    if (conn_fd == -1 || LorieBufferList_findById(&registeredBuffers, id))
        return; // Already registered

    if (conn_fd != -1 && buffer) {
        lorieEvent e = { .type = EVENT_ADD_BUFFER };
        if (!sendData(&e, sizeof(e)) || !LorieBuffer_sendHandleToUnixSocket(buffer, conn_fd)) {
            shutdown(conn_fd, SHUT_RDWR);
            return;
        }
        LorieBuffer_addToList(buffer, &registeredBuffers);
        const LorieBuffer_Desc* desc = LorieBuffer_description(buffer);
        log(INFO, "Sent shared buffer width %d stride %d height %d format %d type %d id %llu", desc->width, desc->stride, desc->height, desc->format, desc->type, desc->id);
    }
}

void lorieUnregisterBuffer(LorieBuffer* buffer) {
    unsigned long id;
    if (!buffer || (!LorieBufferList_findById(&registeredBuffers, (id = LorieBuffer_description(buffer)->id))))
        return;  // Not exist or not registered so no need to unregister

    if (conn_fd != -1 && buffer) {
        lorieEvent e = { .removeBuffer = { .t = EVENT_REMOVE_BUFFER, .id = id } };
        write(conn_fd, &e, sizeof(e));
        LorieBuffer_removeFromList(buffer);
    }
}

extern "C" void DDXNotifyFocusChanged(void) {
    if (conn_fd != -1) {
        lorieEvent e = { .type = EVENT_WINDOW_FOCUS_CHANGED };
        write(conn_fd, &e, sizeof(e));
    }
}

int lorieServerConnect(void) {
    int client[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, client)) return -1;
    if (!QueueWorkProc(+[](__unused ClientPtr pClient, void *closure) -> Bool {
        if (conn_fd != -1) {
            InputThreadUnregisterDev(conn_fd);
            close(conn_fd);
        }
        ++dataConnection;
        lorieSetGpuDoneFd(-1);
        lorieDataReset();
        lorieResetOutputs();
        headerBytes = 0;
        LorieBuffer* buffer;
        while ((buffer = LorieBufferList_first(&registeredBuffers)))
            LorieBuffer_removeFromList(buffer);
        InputThreadRegisterDev((int) (int64_t) closure, handleLorieEvents, nullptr);
        conn_fd = (int) (int64_t) closure;
        lorieActivityConnected();
        return TRUE;
    }, nullptr, (void*) (int64_t) client[1])) { close(client[0]); close(client[1]); return -1; }
    lorieWakeServer();

    return client[0];
}

void lorieEmbeddedServerReady(void) { serverReady(serverContext, display); }

void lorieServerStop(void) {
    if (!QueueWorkProc(+[](__unused ClientPtr, __unused void*) -> Bool {
        GiveUp(0);
        return TRUE;
    }, nullptr, nullptr)) FatalError("Cannot queue X11 shutdown");
    lorieWakeServer();
}

void abort(void) {
    _exit(134);
}

void exit(int code) {
    _exit(code);
}
