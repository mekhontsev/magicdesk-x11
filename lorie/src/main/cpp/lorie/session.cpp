#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <new>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <cstdio>
#include <android/looper.h>
#include "lorie.h"
#include "window_icon.h"

struct SessionConnection {
    Renderer renderer;
    volatile int fd = -1;
    lorieEvent header{};
    size_t headerBytes = 0;
    uint32_t windowIcon[LORIE_WINDOW_ICON_PIXELS]{};
    size_t windowIconBytes = 0;
    bool windowPending = false;
    JNIEnv* env;
    jobject owner;
    jmethodID frameCallback, closedCallback, windowCallback, windowsCallback, dataCallback;

    SessionConnection(JNIEnv* e, jobject object) : env(e), owner(e->NewGlobalRef(object)) {
        jclass cls = env->GetObjectClass(owner);
        frameCallback = env->GetMethodID(cls, "onNativeFrame", "(IIIII)V");
        closedCallback = env->GetMethodID(cls, "onNativeDisconnected", "()V");
        windowCallback = env->GetMethodID(cls, "onNativeWindow", "(I[B[IZZ)V");
        windowsCallback = env->GetMethodID(cls, "onNativeWindowsCommitted", "()V");
        dataCallback = env->GetMethodID(cls, "onNativeData", "(IIIIIIIILjava/lang/String;I)V");
        env->DeleteLocalRef(cls);
        renderer.outputMode = true;
        renderer.init(env, nullptr);
    }

    void disconnect(bool notify) {
        // The renderer acknowledges detachment before its socket descriptor can be reused.
        renderer.setSharedState(nullptr);
        if (fd != -1) {
            ALooper_removeFd(ALooper_forThread(), fd);
            close(fd);
            fd = -1;
        }
        renderer.removeAllBuffers();
        headerBytes = 0;
        windowPending = false; windowIconBytes = 0;
        if (notify) env->CallVoidMethod(owner, closedCallback);
    }

    bool sendAll(const void* bytes, size_t size) {
        size_t sent = 0;
        while (fd >= 0 && sent < size) {
            ssize_t count = send(fd, (const char*)bytes + sent, size - sent, MSG_NOSIGNAL);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { disconnect(true); return false; }
            sent += count;
        }
        return sent == size;
    }

    int receiveWindow() {
        jintArray icon = nullptr;
        if (header.windowInfo.hasIcon) {
            ssize_t count = recv(fd, (char*)windowIcon + windowIconBytes,
                    sizeof(windowIcon) - windowIconBytes, MSG_DONTWAIT);
            if (count < 0 && (errno == EINTR || errno == EAGAIN)) return 1;
            if (count <= 0) { disconnect(true); return 0; }
            windowIconBytes += count;
            if (windowIconBytes != sizeof(windowIcon)) return 1;
            icon = env->NewIntArray(LORIE_WINDOW_ICON_PIXELS);
            if (!icon) { disconnect(true); return 0; }
            env->SetIntArrayRegion(icon, 0, LORIE_WINDOW_ICON_PIXELS, (const jint*)windowIcon);
        }
        size_t size = strnlen(header.windowInfo.title, sizeof(header.windowInfo.title));
        jbyteArray title = env->NewByteArray(size);
        if (!title) { if (icon) env->DeleteLocalRef(icon); disconnect(true); return 0; }
        env->SetByteArrayRegion(title, 0, size, (const jbyte*)header.windowInfo.title);
        env->CallVoidMethod(owner, windowCallback, (jint)header.windowInfo.window,
                title, icon, (jboolean)header.windowInfo.removed, (jboolean)header.windowInfo.mapped);
        env->DeleteLocalRef(title);
        if (icon) env->DeleteLocalRef(icon);
        windowPending = false; windowIconBytes = 0;
        return 1;
    }

    int receive(int events) {
        if (events & (ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP)) { disconnect(true); return 0; }
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
                jstring mime = env->NewStringUTF(header.data.mime);
                if (!mime) { if (descriptor >= 0) close(descriptor); disconnect(true); return 0; }
                env->CallVoidMethod(owner, dataCallback, (jint)header.data.operation, (jint)header.data.channel,
                        (jint)header.data.serial, (jint)header.data.offer, (jint)header.data.output,
                        (jint)header.data.window, (jint)header.data.x, (jint)header.data.y, mime, (jint)descriptor);
                env->DeleteLocalRef(mime);
                break;
            }
            case EVENT_OUTPUT_WINDOWS_DONE: env->CallVoidMethod(owner, windowsCallback); break;
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
                env->CallVoidMethod(owner, frameCallback, (jint)header.frame.output,
                        (jint)header.frame.window, (jint)header.frame.width, (jint)header.frame.height,
                        header.frame.bufferId ? 1 : 0);
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
        if (ALooper_addFd(ALooper_forThread(), fd, 0, ALOOPER_EVENT_INPUT | ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP,
                +[](int, int events, void* data) { return ((SessionConnection*)data)->receive(events); }, this) < 0) {
            disconnect(false);
            return false;
        }
        lorieEvent event{.type = EVENT_RENDERER_WAKEUP_COND};
        if (send(fd, &event, sizeof(event), MSG_NOSIGNAL) != sizeof(event) ||
                ancil_send_fd(fd, renderer.getWakeupCondFd()) < 0) {
            disconnect(false);
            return false;
        }
        event.type = EVENT_GPU_DONE_FD;
        if (send(fd, &event, sizeof(event), MSG_NOSIGNAL) != sizeof(event) ||
                ancil_send_fd(fd, renderer.gpuDoneFd) < 0) {
            disconnect(false);
            return false;
        }
        return true;
    }
};

extern "C" JNIEXPORT jlong JNICALL
Java_com_termux_x11_X11Session_nativeCreate(JNIEnv* env, jobject owner) {
    void* memory = malloc(sizeof(SessionConnection));
    if (!memory) return 0;
    auto* connection = new (memory) SessionConnection(env, owner);
    if (connection->renderer.initialized) return (jlong)connection;
    connection->renderer.destroy();
    env->DeleteGlobalRef(connection->owner);
    connection->~SessionConnection();
    free(connection);
    return 0;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_termux_x11_X11Session_nativeConnect(JNIEnv*, jclass, jlong ptr, jint fd) {
    return ((SessionConnection*)ptr)->connect(fd);
}

extern "C" JNIEXPORT void JNICALL
Java_com_termux_x11_X11Session_nativeSurface(JNIEnv* env, jclass, jlong ptr, jint output, jobject surface, jboolean release) {
    if (!((SessionConnection*)ptr)->renderer.setOutputSurface(env, (uint32_t)output, surface, release) && !env->ExceptionCheck())
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Cannot configure X11 output surface");
}

extern "C" JNIEXPORT void JNICALL
Java_com_termux_x11_X11Session_nativeCommand(JNIEnv*, jclass, jlong ptr, jint output, jint window,
        jint operation, jint x, jint y, jint detail, jboolean down) {
    auto* connection = (SessionConnection*)ptr;
    if (connection->fd < 0) return;
    if (operation == LORIE_OUTPUT_KEY && detail == 0 && x >= 0 && x < 304)
        detail = android_to_linux_keycode[x] ? android_to_linux_keycode[x] + 8 : 0;
    lorieEvent event{.output = {.t = EVENT_OUTPUT_COMMAND, .operation = (uint8_t)operation,
            .down = (uint8_t)down, .output = (uint32_t)output, .window = (uint32_t)window,
            .x = x, .y = y, .detail = (uint16_t)detail}};
    if (send(connection->fd, &event, sizeof(event), MSG_NOSIGNAL) != sizeof(event)) connection->disconnect(true);
}

extern "C" JNIEXPORT void JNICALL
Java_com_termux_x11_X11Session_nativeText(JNIEnv* env, jclass, jlong ptr, jint output, jint window, jstring text) {
    auto* connection = (SessionConnection*)ptr;
    if (connection->fd < 0) return;
    const jchar* chars = env->GetStringChars(text, nullptr);
    jsize length = env->GetStringLength(text);
    for (jsize i = 0; i < length; i++) {
        uint32_t point = chars[i];
        if (point >= 0xd800 && point <= 0xdbff && i + 1 < length && chars[i + 1] >= 0xdc00 && chars[i + 1] <= 0xdfff)
            point = 0x10000 + ((point - 0xd800) << 10) + (chars[++i] - 0xdc00);
        lorieEvent event{.output = {.t = EVENT_OUTPUT_COMMAND, .operation = LORIE_OUTPUT_TEXT,
                .output = (uint32_t)output, .window = (uint32_t)window, .x = (int32_t)point}};
        if (send(connection->fd, &event, sizeof(event), MSG_NOSIGNAL) != sizeof(event)) { connection->disconnect(true); break; }
    }
    env->ReleaseStringChars(text, chars);
}

extern "C" JNIEXPORT void JNICALL
Java_com_termux_x11_X11Session_nativeData(JNIEnv* env, jclass, jlong ptr, jint operation, jint channel,
        jint serial, jint offer, jint output, jint window, jint x, jint y, jstring mime, jint descriptor) {
    auto* connection = (SessionConnection*)ptr;
    if (connection->fd < 0) return;
    lorieEvent event{};
    event.data = {.t = EVENT_DATA, .operation = (uint8_t)operation, .channel = (uint8_t)channel,
            .hasFd = (uint8_t)(descriptor >= 0), .serial = (uint32_t)serial, .offer = (uint32_t)offer,
            .output = (uint32_t)output, .window = (uint32_t)window, .x = x, .y = y};
    if (mime) {
        const char* name = env->GetStringUTFChars(mime, nullptr);
        if (!name) return;
        snprintf(event.data.mime, sizeof(event.data.mime), "%s", name);
        env->ReleaseStringUTFChars(mime, name);
    }
    if (connection->sendAll(&event, sizeof(event)) && descriptor >= 0 && ancil_send_fd(connection->fd, descriptor) < 0)
        connection->disconnect(true);
}

extern "C" JNIEXPORT void JNICALL
Java_com_termux_x11_X11Session_nativeDestroy(JNIEnv* env, jclass, jlong ptr) {
    auto* connection = (SessionConnection*)ptr;
    while (connection->renderer.outputs)
        connection->renderer.setOutputSurface(env, connection->renderer.outputs->id, nullptr, true);
    connection->disconnect(false);
    connection->renderer.destroy();
    env->DeleteGlobalRef(connection->owner);
    connection->~SessionConnection();
    free(connection);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_termux_x11_X11DataExchange_nativeBytes(JNIEnv* env, jclass, jbyteArray bytes) {
    jsize length = env->GetArrayLength(bytes);
    if (length > 1024 * 1024) return -1;
    int fd = (int)syscall(__NR_memfd_create, "x11-content", MFD_CLOEXEC);
    if (fd < 0) return -1;
    jbyte* data = env->GetByteArrayElements(bytes, nullptr);
    if (!data) { close(fd); return -1; }
    bool ok = !length || pwrite(fd, data, length, 0) == length;
    env->ReleaseByteArrayElements(bytes, data, JNI_ABORT);
    if (!ok) { close(fd); return -1; }
    return fd;
}
