#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
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
        // The caller's Surface must not be released until the renderer acknowledges it.
        while (output->changed && !stopping) pthread_cond_wait(&stateChangeFinishCond, &stateLock);
        success = !stopping && (!window || output->surface != EGL_NO_SURFACE);
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
        if (output->surface != EGL_NO_SURFACE) return true;
    return false;
}

bool Renderer::outputsNeedDraw() const {
    for (Output* output = outputs; output; output = output->next)
        if (output->surface != EGL_NO_SURFACE && output->drawnRevision != output->frame.frame.revision) return true;
    return false;
}

void Renderer::invalidateOutputs() {
    for (Output* output = outputs; output; output = output->next) output->drawnRevision = 0;
}

void Renderer::refreshOutputSurfaces() {
    eglMakeCurrent(egl_display, defaultSfc, defaultSfc, ctx);
    for (Output* output = outputs; output; output = output->next) {
        if (!output->changed) continue;
        if (output->surface != EGL_NO_SURFACE) eglDestroySurface(egl_display, output->surface);
        if (output->window) ANativeWindow_release(output->window);
        output->window = output->pending;
        output->pending = nullptr;
        output->surface = output->window ? eglCreateWindowSurface(egl_display, cfg, output->window, nullptr) : EGL_NO_SURFACE;
        output->drawnRevision = 0;
        output->changed = false;
    }
    if (state) state->surfaceAvailable = hasOutputSurface();
}

void Renderer::drawOutputs() {
    // All outputs of this session share the single upstream Present-queue consumer.
    applyPendingGpuCopies();
    pthread_mutex_lock(&stateLock);
    if (connectionFailed || state->waitForNextFrame) { pthread_mutex_unlock(&stateLock); return; }
    struct Draw {
        Draw* next;
        uint32_t id;
        EGLSurface surface;
        ANativeWindow* window;
        lorieEvent frame;
        unsigned layerCount;
        lorieEvent layers[];
    };
    Draw *draws = nullptr, **tail = &draws;
    for (Output* output = outputs; output; output = output->next) {
        if (output->surface == EGL_NO_SURFACE || output->drawnRevision == output->frame.frame.revision) continue;
        auto* draw = (Draw*)malloc(sizeof(Draw) + output->layerCount * sizeof(lorieEvent));
        if (!draw) continue;
        draw->next = nullptr; draw->id = output->id;
        draw->surface = output->surface; draw->window = output->window;
        draw->frame = output->frame; draw->layerCount = output->layerCount;
        memcpy(draw->layers, output->layers, output->layerCount * sizeof(lorieEvent));
        *tail = draw; tail = &draw->next;
    }
    pthread_mutex_unlock(&stateLock);
    // Only this renderer thread replaces EGL surfaces/windows and releases GL
    // buffers, on the next loop iteration. The snapshot borrows those resources,
    // not Output records (which the connection may release after acknowledgement).
    bool rendered = false;
    for (const Draw* item = draws; item; item = item->next) {
        const Draw& draw = *item;
        const auto& frame = draw.frame.frame;
        if (!lockSharedState()) break;
        if (!eglMakeCurrent(egl_display, draw.surface, draw.surface, ctx)) {
            pthread_mutex_unlock(&state->lock);
            continue;
        }
        eglSwapInterval(egl_display, 0);
        glDisable(GL_SCISSOR_TEST);
        int width = ANativeWindow_getWidth(draw.window), height = ANativeWindow_getHeight(draw.window);
        glViewport(0, 0, width, height);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        if (frame.width && frame.height) {
            float x = 1.f, y = 1.f;
            if ((int64_t)width * frame.height > (int64_t)height * frame.width)
                x = (float)height * frame.width / (width * (float)frame.height);
            else y = (float)width * frame.height / (height * (float)frame.width);
            glEnable(GL_SCISSOR_TEST);
            glScissor((int)((1.f - x) * width / 2), (int)((1.f - y) * height / 2),
                    (int)(x * width), (int)(y * height));
            for (unsigned i = 0; i < draw.layerCount; i++) {
                const auto& layer = draw.layers[i].layer;
                pthread_spin_lock(&bufferLock);
                LorieBuffer* buffer = LorieBufferList_findById(&buffers, layer.bufferId);
                pthread_spin_unlock(&bufferLock);
                if (!buffer) continue;
                const auto* desc = LorieBuffer_description(buffer);
                LorieBuffer_bindTexture(buffer);
                float right = desc->type == LORIEBUFFER_FD ? (float)desc->width / desc->stride : 1.f;
                if (layer.alpha) { glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); }
                else glDisable(GL_BLEND);
                drawRegion(0, -x + 2*x*layer.x/frame.width, -y + 2*y*layer.y/frame.height,
                        -x + 2*x*(layer.x + (float)layer.width)/frame.width,
                        -y + 2*y*(layer.y + (float)layer.height)/frame.height,
                        0, 0, right, 1, LorieBuffer_isRgba(buffer));
            }
            glDisable(GL_BLEND);
            glDisable(GL_SCISSOR_TEST);
        }
        EGLSyncKHR fence = eglCreateSyncKHR(egl_display, EGL_SYNC_FENCE_KHR, nullptr);
        glFlush();
        if (fence != EGL_NO_SYNC_KHR) {
            eglClientWaitSyncKHR(egl_display, fence, 0, EGL_FOREVER_KHR);
            eglDestroySyncKHR(egl_display, fence);
        } else glFinish();
        pthread_mutex_unlock(&state->lock);
        eglSwapBuffers(egl_display, draw.surface);
        pthread_mutex_lock(&stateLock);
        for (Output* output = outputs; output; output = output->next)
            if (output->id == draw.id) { output->drawnRevision = frame.revision; break; }
        pthread_mutex_unlock(&stateLock);
        rendered = true;
        state->renderedFrames++;
    }
    eglMakeCurrent(egl_display, defaultSfc, defaultSfc, ctx);
    while (draws) { Draw* next = draws->next; free(draws); draws = next; }
    if (rendered) state->waitForNextFrame = true;
}
