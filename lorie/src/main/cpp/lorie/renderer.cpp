#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include "lorie.h"
#include "gpu_completion.h"

#define loge(...) __android_log_print(ANDROID_LOG_ERROR, "x11-renderer", __VA_ARGS__)

void Renderer::notifyGpuCopyDone() const {
    if (!lorieNotifyGpuCompletion(gpuDoneFd)) loge("GPU completion notification failed: %s", strerror(errno));
}

void* Renderer::initThread() {
    graphicsDevice = graphics->create();
    if (!graphicsDevice) return nullptr;
    pthread_mutex_lock(&stateLock);
    initialized = true;
    pthread_cond_broadcast(&stateChangeFinishCond);
    threadLoop();
    return nullptr;
}

bool Renderer::init() {
    if (thread) return initialized;
    xorg_list_init(&addedBuffers);
    xorg_list_init(&buffers);
    xorg_list_init(&removedBuffers);
    pthread_mutex_init(&stateLock, nullptr);
    pthread_cond_init(&stateChangeFinishCond, nullptr);
    pthread_spin_init(&bufferLock, false);
    pthread_condattr_t attributes;
    pthread_condattr_init(&attributes);
    pthread_condattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED);
    stateCondFd = LorieBuffer_createRegion("renderer-cond", sizeof(pthread_cond_t));
    stateCond = stateCondFd < 0 ? nullptr : (pthread_cond_t*)mmap(nullptr, sizeof(pthread_cond_t),
            PROT_READ | PROT_WRITE, MAP_SHARED, stateCondFd, 0);
    if (stateCond == MAP_FAILED) stateCond = nullptr;
    if (stateCond) pthread_cond_init(stateCond, &attributes);
    pthread_condattr_destroy(&attributes);
    gpuDoneFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (!stateCond || gpuDoneFd < 0) return false;
    stopping = initialized = false;
    pthread_mutex_lock(&stateLock);
    int error = pthread_create(&thread, nullptr, +[](void* cookie) -> void* {
        auto* renderer = (Renderer*)cookie;
        renderer->initThread();
        renderer->releaseGraphics();
        pthread_mutex_lock(&renderer->stateLock);
        renderer->stopping = true;
        pthread_cond_broadcast(&renderer->stateChangeFinishCond);
        pthread_mutex_unlock(&renderer->stateLock);
        return nullptr;
    }, this);
    if (error) { thread = 0; stopping = true; }
    // EVENT_WAIT: the graphics worker acknowledges initialization or exits.
    while (!initialized && !stopping) pthread_cond_wait(&stateChangeFinishCond, &stateLock);
    pthread_mutex_unlock(&stateLock);
    return initialized;
}

void Renderer::destroy() {
    pthread_mutex_lock(&stateLock);
    stopping = true;
    if (stateCond) pthread_cond_signal(stateCond);
    pthread_mutex_unlock(&stateLock);
    if (thread) pthread_join(thread, nullptr);
    thread = 0;
    if (stateCond) munmap(stateCond, sizeof(pthread_cond_t));
    if (stateCondFd >= 0) close(stateCondFd);
    if (gpuDoneFd >= 0) close(gpuDoneFd);
    stateCond = nullptr;
    stateCondFd = gpuDoneFd = -1;
    pthread_cond_destroy(&stateChangeFinishCond);
    pthread_mutex_destroy(&stateLock);
    pthread_spin_destroy(&bufferLock);
}

int Renderer::getWakeupCondFd() const { return stateCondFd; }

void rendererTestCapabilities(int* legacyDrawing, int* gpuPresentDisabled) {
    AHardwareBuffer_Desc description = {.width = 64, .height = 64, .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
            AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN};
    AHardwareBuffer* buffer = nullptr;
    void* pixels = nullptr;
    bool available = AHardwareBuffer_allocate(&description, &buffer) == 0 && buffer &&
        AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
            AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &pixels) == 0;
    if (available) AHardwareBuffer_unlock(buffer, nullptr);
    if (buffer) AHardwareBuffer_release(buffer);
    if (!available) { *legacyDrawing = 1; *gpuPresentDisabled = 1; }
}

void Renderer::setSharedState(lorie_shared_server_state* newState) {
    pthread_mutex_lock(&stateLock);
    pendingState = newState;
    stateChanged = true;
    pthread_cond_signal(stateCond);
    // EVENT_WAIT: the renderer releases the previous shared mapping before acknowledgement.
    while (stateChanged && !stopping) pthread_cond_wait(&stateChangeFinishCond, &stateLock);
    pthread_mutex_unlock(&stateLock);
}

void Renderer::addBuffer(LorieBuffer* buffer) {
    pthread_mutex_lock(&stateLock);
    pthread_spin_lock(&bufferLock);
    LorieBuffer_addToList(buffer, &addedBuffers);
    pthread_spin_unlock(&bufferLock);
    pthread_cond_signal(stateCond);
    pthread_mutex_unlock(&stateLock);
}

void Renderer::removeBuffer(uint64_t id) {
    pthread_mutex_lock(&stateLock);
    pthread_spin_lock(&bufferLock);
    LorieBuffer* buffer = LorieBufferList_findById(&addedBuffers, id);
    if (buffer) LorieBuffer_release(buffer);
    else if ((buffer = LorieBufferList_findById(&buffers, id)))
        LorieBuffer_addToList(buffer, &removedBuffers);
    pthread_spin_unlock(&bufferLock);
    pthread_cond_signal(stateCond);
    pthread_mutex_unlock(&stateLock);
}

void Renderer::removeAllBuffers() {
    pthread_spin_lock(&bufferLock);
    LorieBuffer* buffer;
    while ((buffer = LorieBufferList_first(&addedBuffers))) LorieBuffer_release(buffer);
    while ((buffer = LorieBufferList_first(&buffers))) LorieBuffer_addToList(buffer, &removedBuffers);
    pthread_spin_unlock(&bufferLock);
}

void Renderer::attachBuffer(LorieBuffer* buffer) {
    if (LorieBuffer_graphicsImage(buffer)) return;
    const auto* description = LorieBuffer_description(buffer);
    void* image = graphics->image(graphicsDevice, description->buffer, description->data,
            description->width, description->height, (size_t)description->stride * 4, description->format);
    LorieBuffer_setGraphicsImage(buffer, image, graphics->releaseImage);
}

LorieBuffer* Renderer::findBuffer(uint64_t id) {
    pthread_spin_lock(&bufferLock);
    LorieBuffer* buffer = LorieBufferList_findById(&buffers, id);
    if (!buffer && (buffer = LorieBufferList_findById(&addedBuffers, id)))
        LorieBuffer_addToList(buffer, &buffers);
    pthread_spin_unlock(&bufferLock);
    // Only this worker frees imported buffers, including those concurrently retired.
    if (buffer) attachBuffer(buffer);
    return buffer;
}

uint64_t Renderer::applyPendingGpuCopiesLocked() {
    uint64_t completed = 0;
    while (state->gpuCopyQueue.readIndex != __atomic_load_n(&state->gpuCopyQueue.writeIndex, __ATOMIC_ACQUIRE)) {
        const auto& entry = state->gpuCopyQueue.entries[state->gpuCopyQueue.readIndex % LORIE_GPU_COPY_QUEUE_CAPACITY];
        LorieBuffer* source = findBuffer(entry.srcBufferId);
        LorieBuffer* target = findBuffer(entry.dstBufferId);
        if (!source || !target) {
            // EVENT_WAIT: registration socket wakes the worker; retain the queue entry and its serial.
            copyWaitingForBuffer = true;
            break;
        }
        const auto* destination = LorieBuffer_description(target);
        float clear[4] = {};
        void* pass = graphics->begin(graphicsDevice, LorieBuffer_graphicsImage(target), clear, true);
        bool ok = pass && entry.numRects <= LORIE_GPU_COPY_MAX_RECTS;
        for (unsigned i = 0; ok && i < entry.numRects; ++i) {
            const auto& rect = entry.rects[i];
            LorieGraphicsDraw draw = {.image = LorieBuffer_graphicsImage(source),
                .sx = (float)rect.x1, .sy = (float)rect.y1,
                .sw = (float)(rect.x2 - rect.x1), .sh = (float)(rect.y2 - rect.y1),
                .x = (float)(rect.x1 + entry.xOff), .y = (float)(rect.y1 + entry.yOff),
                .width = (float)(rect.x2 - rect.x1), .height = (float)(rect.y2 - rect.y1),
                .clipWidth = destination->width, .clipHeight = destination->height,
                .swapRedBlue = LorieBuffer_isRgba(source) != LorieBuffer_isRgba(target)};
            ok = graphics->draw(pass, &draw);
        }
        if (ok) ok = graphics->submit(pass);
        else if (pass) graphics->cancel(pass);
        if (!ok) {
            loge("Present copy failed; ending connection without acknowledging buffer reuse");
            connectionFailed = true;
            if (peerFd >= 0) shutdown(peerFd, SHUT_RDWR);
            break;
        }
        completed = entry.serial;
        ++state->gpuCopyQueue.readIndex;
    }
    return completed;
}

bool Renderer::lockSharedState() {
    int error = lorieLockShared(&state->lock, +[](void* context) {
        auto* renderer = (Renderer*)context;
        pollfd peer = {.fd = renderer->peerFd, .events = POLLIN | POLLRDHUP};
        int result;
        do { result = poll(&peer, 1, 0); } while (result < 0 && errno == EINTR);
        return peer.fd >= 0 && result >= 0 && !(peer.revents & (POLLHUP | POLLRDHUP | POLLERR | POLLNVAL));
    }, this);
    if (!error) return true;
    loge("X11 renderer buffer lock failed: %s", strerror(error));
    connectionFailed = true;
    if (peerFd >= 0) shutdown(peerFd, SHUT_RDWR);
    return false;
}

void Renderer::applyPendingGpuCopies() {
    if (!state || connectionFailed || state->gpuCopyQueue.readIndex == state->gpuCopyQueue.writeIndex) return;
    if (!lockSharedState()) return;
    uint64_t serial = applyPendingGpuCopiesLocked();
    if (serial) {
        // submit() has completed GPU reads before the producer may reuse its source.
        __atomic_store_n(&state->gpuCopyQueue.completedSerial, serial, __ATOMIC_RELEASE);
        notifyGpuCopyDone();
    }
    pthread_mutex_unlock(&state->lock);
}

bool Renderer::shouldWait() {
    pthread_spin_lock(&bufferLock);
    bool buffersChanged = !xorg_list_is_empty(&addedBuffers) || !xorg_list_is_empty(&removedBuffers);
    pthread_spin_unlock(&bufferLock);
    if (stateChanged || buffersChanged || outputSurfacesChanged()) return false;
    if (connectionFailed || copyWaitingForBuffer) return true;
    if (state && state->gpuCopyQueue.readIndex != state->gpuCopyQueue.writeIndex) return false;
    return !state || state->waitForNextFrame || !outputsNeedDraw();
}

void Renderer::threadLoop() {
    while (!stopping) {
        // EVENT_WAIT: server damage/vsync, buffer registration or host surface changes.
        while (!stopping && shouldWait()) pthread_cond_wait(stateCond, &stateLock);
        if (stopping) break;
        if (stateChanged) {
            if (state && state != pendingState) {
                state->surfaceAvailable = false;
                munmap(state, sizeof(*state));
            }
            state = pendingState;
            pendingState = nullptr;
            stateChanged = connectionFailed = copyWaitingForBuffer = false;
            if (state) state->surfaceAvailable = hasOutputSurface();
        }
        refreshOutputSurfaces();
        for (;;) {
            pthread_spin_lock(&bufferLock);
            LorieBuffer* buffer = LorieBufferList_first(&addedBuffers);
            if (buffer) LorieBuffer_addToList(buffer, &buffers);
            pthread_spin_unlock(&bufferLock);
            if (!buffer) break;
            attachBuffer(buffer);
            copyWaitingForBuffer = false;
            invalidateOutputs();
        }
        pthread_cond_broadcast(&stateChangeFinishCond);
        pthread_mutex_unlock(&stateLock);
        if (state) drawOutputs();
        for (;;) {
            pthread_spin_lock(&bufferLock);
            LorieBuffer* buffer = LorieBufferList_first(&removedBuffers);
            if (buffer) LorieBuffer_removeFromList(buffer);
            pthread_spin_unlock(&bufferLock);
            if (!buffer) break;
            LorieBuffer_release(buffer);
        }
        pthread_mutex_lock(&stateLock);
    }
    pthread_mutex_unlock(&stateLock);
}

void Renderer::releaseGraphics() {
    removeAllBuffers();
    LorieBuffer* buffer;
    while ((buffer = LorieBufferList_first(&removedBuffers))) LorieBuffer_release(buffer);
    for (Output* output = outputs; output; output = output->next) {
        if (output->surface) graphics->releaseSurface(output->surface);
        if (output->window) ANativeWindow_release(output->window);
        if (output->pending) ANativeWindow_release(output->pending);
        output->surface = nullptr;
        output->window = output->pending = nullptr;
    }
    if (state) { munmap(state, sizeof(*state)); state = nullptr; }
    free(draws);
    draws = nullptr;
    drawCapacity = 0;
    if (graphicsDevice) graphics->destroy(graphicsDevice);
    graphicsDevice = nullptr;
}
