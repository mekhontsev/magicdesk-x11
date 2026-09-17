#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <new>
#include <sys/mman.h>
#include <cstdio>
#include <android/looper.h>
#include "command_queue.h"
#include "lorie.h"
#include "embedded.h"
#include "window_icon.h"

struct LorieConnection {
    Renderer renderer;
    volatile int fd = -1;
    lorieEvent header{};
    size_t headerBytes = 0;
    uint32_t windowIcon[LORIE_WINDOW_ICON_PIXELS]{};
    size_t windowIconBytes = 0;
    bool windowPending = false;
    LorieCommandQueue commands;
    LorieCallbacks callbacks;
    void* context;

    LorieConnection(const LorieCallbacks& cb, void* owner) : callbacks(cb), context(owner) {
        renderer.init();
    }

    void disconnect(bool notify) {
        // Cancel a renderer lock wait before waiting for Surface/shared-state
        // release. The descriptor remains valid until that acknowledgement.
        if (fd >= 0) shutdown(fd, SHUT_RDWR);
        // The renderer acknowledges detachment before its socket descriptor can be reused.
        renderer.setSharedState(nullptr);
        renderer.peerFd = -1;
        if (fd != -1) {
            ALooper_removeFd(ALooper_forThread(), fd);
            close(fd);
            fd = -1;
        }
        renderer.removeAllBuffers();
        commands.clear();
        headerBytes = 0;
        windowPending = false; windowIconBytes = 0;
        if (notify) callbacks.disconnected(context);
    }

    bool watch() {
        if (fd < 0) return false;
        int events = ALOOPER_EVENT_INPUT | ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP;
        if (!commands.empty()) events |= ALOOPER_EVENT_OUTPUT;
        return ALooper_addFd(ALooper_forThread(), fd, 0, events,
                +[](int, int ready, void* data) { return ((LorieConnection*)data)->receive(ready); }, this) >= 0;
    }

    bool sendAll(const void* bytes, size_t size, int descriptor = -1) {
        if (fd < 0) return false;
        if (!commands.append(bytes, size, descriptor) || !commands.flush(fd) || !watch()) {
            disconnect(true);
            return false;
        }
        return true;
    }

    int receiveWindow() {
        if (header.windowInfo.hasIcon) {
            ssize_t count = recv(fd, (char*)windowIcon + windowIconBytes,
                    sizeof(windowIcon) - windowIconBytes, MSG_DONTWAIT);
            if (count < 0 && (errno == EINTR || errno == EAGAIN)) return 1;
            if (count <= 0) { disconnect(true); return 0; }
            windowIconBytes += count;
            if (windowIconBytes != sizeof(windowIcon)) return 1;
        }
        header.windowInfo.title[sizeof(header.windowInfo.title) - 1] = 0;
        callbacks.window(context, header.windowInfo.window, header.windowInfo.title,
                header.windowInfo.hasIcon ? windowIcon : nullptr,
                header.windowInfo.removed, header.windowInfo.mapped, header.windowInfo.hostManaged,
                header.windowInfo.fullscreenSerial, header.windowInfo.fullscreenRequested,
                header.windowInfo.fullscreenActual);
        windowPending = false; windowIconBytes = 0;
        return 1;
    }

    int receive(int events) {
        if (events & (ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP)) { disconnect(true); return 0; }
        if (events & ALOOPER_EVENT_OUTPUT) {
            if (!commands.flush(fd) || !watch()) { disconnect(true); return 0; }
        }
        if (!(events & ALOOPER_EVENT_INPUT)) return 1;
        if (windowPending) return receiveWindow();
        ssize_t count = recv(fd, (char*)&header + headerBytes, sizeof(header) - headerBytes, MSG_DONTWAIT);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) return 1;
        if (count <= 0) { disconnect(true); return 0; }
        headerBytes += count;
        if (headerBytes != sizeof(header)) return 1;
        headerBytes = 0;
        switch (header.type) {
            case EVENT_DATA: {
                int descriptor = header.data.hasFd ? ancil_recv_fd(fd) : -1;
                if (header.data.hasFd && descriptor < 0) { disconnect(true); return 0; }
                if (!memchr(header.data.mime, 0, sizeof(header.data.mime))) {
                    if (descriptor >= 0) close(descriptor);
                    disconnect(true); return 0;
                }
                callbacks.data(context, header.data.operation, header.data.channel, header.data.serial,
                        header.data.offer, header.data.output, header.data.window, header.data.x, header.data.y,
                        header.data.mime, descriptor);
                break;
            }
            case EVENT_OUTPUT_WINDOWS_DONE: callbacks.windowsCommitted(context); break;
            case EVENT_SHARED_SERVER_STATE: {
                int sharedFd = ancil_recv_fd(fd);
                if (sharedFd < 0) { disconnect(true); return 0; }
                auto* state = (lorie_shared_server_state*)mmap(nullptr, sizeof(lorie_shared_server_state),
                        PROT_READ | PROT_WRITE, MAP_SHARED, sharedFd, 0);
                close(sharedFd);
                if (state == MAP_FAILED) { disconnect(true); return 0; }
                renderer.setSharedState(state);
                break;
            }
            case EVENT_ADD_BUFFER: {
                LorieBuffer* buffer = nullptr;
                LorieBuffer_recvHandleFromUnixSocket(fd, &buffer);
                if (!buffer) { disconnect(true); return 0; }
                renderer.addBuffer(buffer);
                break;
            }
            case EVENT_REMOVE_BUFFER: renderer.removeBuffer(header.removeBuffer.id); break;
            case EVENT_OUTPUT_FRAME:
                renderer.setOutputFrame(header);
                callbacks.frame(context, header.frame.output, header.frame.window, header.frame.width,
                        header.frame.height, header.frame.bufferId != 0);
                break;
            case EVENT_OUTPUT_LAYER: renderer.setOutputFrame(header); break;
            case EVENT_OUTPUT_WINDOW: {
                windowPending = true;
                return receiveWindow();
            }
            case EVENT_WINDOW_FOCUS_CHANGED:
            case EVENT_SYNC_REPLY: break;
            default: disconnect(true); return 0;
        }
        return 1;
    }

    bool connect(int incoming) {
        disconnect(false);
        fd = incoming;
        if (fd < 0) return false;
        renderer.peerFd = fd;
        if (!watch()) {
            disconnect(false);
            return false;
        }
        lorieEvent event{.type = EVENT_RENDERER_WAKEUP_COND};
        if (!sendAll(&event, sizeof(event), renderer.getWakeupCondFd())) {
            disconnect(false);
            return false;
        }
        event.type = EVENT_GPU_DONE_FD;
        if (!sendAll(&event, sizeof(event), renderer.gpuDoneFd)) {
            disconnect(false);
            return false;
        }
        return true;
    }
};

LorieConnection* lorieConnectionCreate(const LorieCallbacks* callbacks, void* context) {
    if (!callbacks || !callbacks->frame || !callbacks->disconnected || !callbacks->window ||
            !callbacks->windowsCommitted || !callbacks->data) return nullptr;
    void* memory = malloc(sizeof(LorieConnection));
    if (!memory) return nullptr;
    auto* connection = new (memory) LorieConnection(*callbacks, context);
    if (connection->renderer.initialized) return connection;
    connection->renderer.destroy();
    connection->~LorieConnection();
    free(connection);
    return nullptr;
}

bool lorieConnectionConnect(LorieConnection* connection, int fd) { return connection->connect(fd); }

bool lorieConnectionSurface(LorieConnection* connection, uint32_t output, ANativeWindow* window, bool release) {
    return connection->renderer.setOutputSurface(output, window, release);
}

void lorieConnectionCommand(LorieConnection* connection, uint32_t output, uint32_t window,
        int operation, int x, int y, int detail, bool down) {
    if (connection->fd < 0) return;
    lorieEvent event{.output = {.t = EVENT_OUTPUT_COMMAND, .operation = (uint8_t)operation,
            .down = (uint8_t)down, .output = output, .window = window,
            .x = x, .y = y, .detail = (uint16_t)detail}};
    connection->sendAll(&event, sizeof(event));
}

void lorieConnectionData(LorieConnection* connection, int operation, int channel,
        uint32_t serial, uint32_t offer, uint32_t output, uint32_t window,
        int x, int y, const char* type, int descriptor) {
    if (connection->fd < 0) return;
    lorieEvent event{};
    event.data = {.t = EVENT_DATA, .operation = (uint8_t)operation, .channel = (uint8_t)channel,
            .hasFd = (uint8_t)(descriptor >= 0), .serial = serial, .offer = offer,
            .output = output, .window = window, .x = x, .y = y};
    snprintf(event.data.mime, sizeof(event.data.mime), "%s", type ? type : "");
    connection->sendAll(&event, sizeof(event), descriptor);
}

void lorieConnectionDestroy(LorieConnection* connection) {
    if (!connection) return;
    connection->disconnect(false);
    while (connection->renderer.outputs)
        connection->renderer.setOutputSurface(connection->renderer.outputs->id, nullptr, true);
    connection->renderer.destroy();
    connection->~LorieConnection();
    free(connection);
}
