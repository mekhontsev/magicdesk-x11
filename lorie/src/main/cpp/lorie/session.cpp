#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <new>
#include <sys/mman.h>
#include <android/looper.h>
#include "lorie.h"

struct SessionConnection {
    Renderer renderer;
    volatile int fd = -1;
    lorieEvent header{};
    size_t headerBytes = 0;
    JNIEnv* env;
    jobject owner;
    jmethodID frameCallback, closedCallback;

    SessionConnection(JNIEnv* e, jobject object) : env(e), owner(e->NewGlobalRef(object)) {
        jclass cls = env->GetObjectClass(owner);
        frameCallback = env->GetMethodID(cls, "onNativeFrame", "(IIIII)V");
        closedCallback = env->GetMethodID(cls, "onNativeDisconnected", "()V");
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
        if (notify) env->CallVoidMethod(owner, closedCallback);
    }

    int receive(int events) {
        if (events & (ALOOPER_EVENT_ERROR | ALOOPER_EVENT_HANGUP)) { disconnect(true); return 0; }
        ssize_t count = recv(fd, (char*)&header + headerBytes, sizeof(header) - headerBytes, MSG_DONTWAIT);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) return 1;
        if (count <= 0) { disconnect(true); return 0; }
        headerBytes += count;
        if (headerBytes != sizeof(header)) return 1;
        headerBytes = 0;
        switch (header.type) {
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
        renderer.connFdPtr = &fd;
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
