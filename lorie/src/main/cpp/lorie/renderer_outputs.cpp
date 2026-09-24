#include <cstdlib>
#include <cstring>
#include <new>
#include "lorie.h"

bool Renderer::setOutputSurface(uint32_t id, ANativeWindow* window, bool release) {
    if (!id) return false;
    if (window) ANativeWindow_acquire(window);
    bool success = release;
    pthread_mutex_lock(&stateLock);
    Output* output = outputs;
    while (output && output->id != id) output = output->next;
    if (!output && !release) {
        void* memory = calloc(1, sizeof(Output));
        if (memory) {
            output = new (memory) Output();
            output->id = id;
            output->next = outputs;
            outputs = output;
        }
    }
    if (output) {
        output->pending = window;
        output->released = release;
        output->changed = true;
        pthread_cond_signal(stateCond);
        // EVENT_WAIT: worker releases the previous Surface before the caller may destroy it.
        while (output->changed && !stopping) pthread_cond_wait(&stateChangeFinishCond, &stateLock);
        success = !stopping && (!window || output->surface);
        if (release) {
            Output** link = &outputs;
            while (*link != output) link = &(*link)->next;
            *link = output->next;
            output->~Output();
            free(output);
        }
    } else if (window) ANativeWindow_release(window);
    pthread_mutex_unlock(&stateLock);
    return success;
}

void Renderer::setOutputFrame(const lorieEvent& event) {
    pthread_mutex_lock(&stateLock);
    for (Output* output = outputs; output; output = output->next) {
        if (output->id != event.frame.output) continue;
        if (event.type == EVENT_OUTPUT_LAYER) {
            if (output->pendingCount < LORIE_MAX_FAMILY_LAYERS) output->pendingLayers[output->pendingCount++] = event;
        } else {
            output->frame = event;
            output->layerCount = output->pendingCount;
            memcpy(output->layers, output->pendingLayers, output->layerCount * sizeof(lorieEvent));
            output->pendingCount = 0;
        }
        break;
    }
    pthread_cond_signal(stateCond);
    pthread_mutex_unlock(&stateLock);
}

bool Renderer::outputSurfacesChanged() const {
    for (Output* output = outputs; output; output = output->next) if (output->changed) return true;
    return false;
}

bool Renderer::hasOutputSurface() const {
    for (Output* output = outputs; output; output = output->next)
        if (output->surface) return true;
    return false;
}

bool Renderer::outputsNeedDraw() const {
    for (Output* output = outputs; output; output = output->next)
        if (output->surface && output->drawnRevision != output->frame.frame.revision) return true;
    return false;
}

void Renderer::invalidateOutputs() {
    for (Output* output = outputs; output; output = output->next) output->drawnRevision = 0;
}

void Renderer::refreshOutputSurfaces() {
    for (Output* output = outputs; output; output = output->next) {
        if (!output->changed) continue;
        if (output->surface) graphics->releaseSurface(output->surface);
        if (output->window) ANativeWindow_release(output->window);
        output->window = output->pending;
        output->pending = nullptr;
        output->surface = output->window ? graphics->surface(graphicsDevice, output->window) : nullptr;
        output->drawnRevision = 0;
        output->changed = false;
    }
    if (state) state->surfaceAvailable = hasOutputSurface();
}

void Renderer::drawOutputs() {
    applyPendingGpuCopies();
    pthread_mutex_lock(&stateLock);
    if (connectionFailed || copyWaitingForBuffer || state->waitForNextFrame) {
        pthread_mutex_unlock(&stateLock);
        return;
    }
    size_t count = 0;
    for (Output* output = outputs; output; output = output->next) if (output->surface) ++count;
    if (count > drawCapacity) {
        auto* memory = (Draw*)realloc(draws, count * sizeof(Draw));
        if (!memory) { pthread_mutex_unlock(&stateLock); return; }
        draws = memory;
        drawCapacity = count;
    }
    count = 0;
    for (Output* output = outputs; output; output = output->next) {
        if (!output->surface || output->drawnRevision == output->frame.frame.revision) continue;
        Draw& draw = draws[count++];
        draw.id = output->id; draw.surface = output->surface; draw.window = output->window;
        draw.frame = output->frame; draw.layerCount = output->layerCount;
        memcpy(draw.layers, output->layers, draw.layerCount * sizeof(lorieEvent));
    }
    pthread_mutex_unlock(&stateLock);
    // Only this worker replaces surfaces and releases images. Reused snapshots
    // borrow those resources, never the connection-owned Output records.
    bool rendered = false;
    for (size_t index = 0; index < count; ++index) {
        const Draw& draw = draws[index];
        const auto& frame = draw.frame.frame;
        int width = ANativeWindow_getWidth(draw.window), height = ANativeWindow_getHeight(draw.window);
        // Android buffer acquisition/presentation must not hold the shared X pixel lock.
        void* target = graphics->acquire(draw.surface, width, height);
        if (!target) {
            connectionFailed = true;
            if (peerFd >= 0) shutdown(peerFd, SHUT_RDWR);
            break;
        }
        if (!lockSharedState()) break;
        float clear[4] = {0, 0, 0, frame.presentation ? 0.f : 1.f};
        void* pass = graphics->begin(graphicsDevice, target, clear, false);
        bool ok = pass != nullptr;
        bool complete = frame.width && frame.height && draw.layerCount;
        int contentWidth = frame.presentation ? frame.viewport.right - frame.viewport.left : frame.width;
        int contentHeight = frame.presentation ? frame.viewport.bottom - frame.viewport.top : frame.height;
        if (ok && contentWidth > 0 && contentHeight > 0) {
            int originX = frame.presentation ? frame.viewport.left : 0;
            int originY = frame.presentation ? frame.viewport.top : 0;
            float scale = (float)width / contentWidth;
            if ((float)height / contentHeight < scale) scale = (float)height / contentHeight;
            float left = (width - scale * contentWidth) / 2, top = (height - scale * contentHeight) / 2;
            for (unsigned i = 0; ok && i < draw.layerCount; ++i) {
                const auto& layer = draw.layers[i].layer;
                LorieBuffer* buffer = findBuffer(layer.bufferId);
                if (!buffer) { complete = false; continue; }
                const auto* description = LorieBuffer_description(buffer);
                LorieGraphicsDraw command = {.image = LorieBuffer_graphicsImage(buffer),
                    .sw = (float)description->width, .sh = (float)description->height,
                    .x = left + (layer.x - originX) * scale, .y = top + (layer.y - originY) * scale,
                    .width = layer.width * scale, .height = layer.height * scale,
                    .clipX = (int)left, .clipY = (int)top,
                    .clipWidth = (int)(scale * contentWidth), .clipHeight = (int)(scale * contentHeight),
                    .blend = (bool)layer.alpha, .linear = filtering, .swapRedBlue = LorieBuffer_isRgba(buffer)};
                ok = graphics->draw(pass, &command);
            }
        }
        if (ok) ok = graphics->submit(pass);
        else if (pass) graphics->cancel(pass);
        pthread_mutex_unlock(&state->lock);
        bool presented = ok && graphics->present(draw.surface);
        bool acknowledge = false;
        pthread_mutex_lock(&stateLock);
        for (Output* output = outputs; output; output = output->next) if (output->id == draw.id) {
            output->drawnRevision = frame.revision;
            acknowledge = frame.presentation && complete && output->presented != frame.presentation;
            if (acknowledge) output->presented = frame.presentation;
            break;
        }
        pthread_mutex_unlock(&stateLock);
        if (acknowledge && presentationCallback)
            presentationCallback(presentationContext, draw.id, frame.presentation, presented);
        if (!ok) {
            connectionFailed = true;
            if (peerFd >= 0) shutdown(peerFd, SHUT_RDWR);
            break;
        }
        rendered |= presented;
        if (presented) ++state->renderedFrames;
    }
    if (rendered) state->waitForNextFrame = true;
}
