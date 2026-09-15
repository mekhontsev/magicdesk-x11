#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cstdlib>
#include <new>
#include "lorie.h"

bool Renderer::setOutputSurface(JNIEnv* env, uint32_t id, jobject surface, bool release) {
    if (!id) return false;
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    if (surface && !window) return false;
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
        if (output->id == event.frame.output) { output->frame = event; break; }
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
    if (state->waitForNextFrame) { pthread_mutex_unlock(&stateLock); return; }
    bool rendered = false;
    for (Output* output = outputs; output; output = output->next) {
        const auto& frame = output->frame.frame;
        if (output->surface == EGL_NO_SURFACE || output->drawnRevision == frame.revision) continue;
        lorie_mutex_lock(&state->lock, &state->lockingPid);
        pthread_spin_lock(&bufferLock);
        LorieBuffer* buffer = frame.width && frame.height ? LorieBufferList_findById(&buffers, frame.bufferId) : nullptr;
        pthread_spin_unlock(&bufferLock);
        if (!eglMakeCurrent(egl_display, output->surface, output->surface, ctx)) {
            output->drawnRevision = frame.revision;
            lorie_mutex_unlock(&state->lock, &state->lockingPid);
            continue;
        }
        eglSwapInterval(egl_display, 0);
        glDisable(GL_SCISSOR_TEST);
        int width = ANativeWindow_getWidth(output->window), height = ANativeWindow_getHeight(output->window);
        glViewport(0, 0, width, height);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        if (buffer) {
            const auto* desc = LorieBuffer_description(buffer);
            LorieBuffer_bindTexture(buffer);
            float right = desc->type == LORIEBUFFER_FD ? (float)desc->width / desc->stride : 1.f;
            float x = 1.f, y = 1.f;
            if ((int64_t)width * frame.height > (int64_t)height * frame.width)
                x = (float)height * frame.width / (width * (float)frame.height);
            else y = (float)width * frame.height / (height * (float)frame.width);
            drawRegion(0, -x, -y, x, y, 0, 0, right, 1, LorieBuffer_isRgba(buffer));
        }
        EGLSyncKHR fence = eglCreateSyncKHR(egl_display, EGL_SYNC_FENCE_KHR, nullptr);
        glFlush();
        if (fence != EGL_NO_SYNC_KHR) {
            eglClientWaitSyncKHR(egl_display, fence, 0, EGL_FOREVER_KHR);
            eglDestroySyncKHR(egl_display, fence);
        } else glFinish();
        lorie_mutex_unlock(&state->lock, &state->lockingPid);
        eglSwapBuffers(egl_display, output->surface);
        output->drawnRevision = frame.revision;
        rendered = true;
        state->renderedFrames++;
    }
    eglMakeCurrent(egl_display, defaultSfc, defaultSfc, ctx);
    if (rendered) state->waitForNextFrame = true;
    pthread_mutex_unlock(&stateLock);
}
