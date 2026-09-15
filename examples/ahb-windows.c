#define EGL_NO_PLATFORM_SPECIFIC_TYPES
#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }
static void require(bool ok, const char* operation) {
    if (!ok) { fprintf(stderr, "FAIL %s egl=%x gl=%x\n", operation, eglGetError(), glGetError()); exit(1); }
}
static void checked(xcb_connection_t* c, xcb_void_cookie_t cookie, const char* name) {
    xcb_generic_error_t* error = xcb_request_check(c, cookie);
    if (error) { fprintf(stderr, "FAIL %s Xerror=%u\n", name, error->error_code); exit(1); }
}
typedef struct {
    xcb_window_t window;
    xcb_pixmap_t pixmap;
    AHardwareBuffer* buffer;
    EGLImageKHR image;
    GLuint texture, framebuffer;
    int width, height, requestedWidth, requestedHeight;
    bool busy;
    uint32_t serial;
} Output;

static void release_buffer(xcb_connection_t* c, EGLDisplay display, Output* out) {
    if (!out->buffer) return;
    xcb_free_pixmap(c, out->pixmap);
    glDeleteFramebuffers(1, &out->framebuffer);
    glDeleteTextures(1, &out->texture);
    eglDestroyImageKHR(display, out->image);
    AHardwareBuffer_release(out->buffer);
    out->buffer = NULL;
}

static void allocate_buffer(xcb_connection_t* c, EGLDisplay display, Output* out, int slot) {
    release_buffer(c, display, out);
    AHardwareBuffer_Desc desc = {.width=out->requestedWidth, .height=out->requestedHeight,
        // Termux:X11 accepts HAL_PIXEL_FORMAT_BGRA_8888 (5), matching its root visual.
        .layers=1, .format=5,
        .usage=AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT
            | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN};
    require(!AHardwareBuffer_allocate(&desc, &out->buffer), "AHB allocate");
    AHardwareBuffer_describe(out->buffer, &desc);
    out->width = desc.width;
    out->height = desc.height;
    EGLClientBuffer client = eglGetNativeClientBufferANDROID(out->buffer);
    EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    out->image = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client, attrs);
    require(out->image != EGL_NO_IMAGE_KHR, "EGL image");
    glGenTextures(1, &out->texture);
    glBindTexture(GL_TEXTURE_2D, out->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, out->image);
    glGenFramebuffers(1, &out->framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, out->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out->texture, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "AHB framebuffer");

    int sockets[2];
    require(!socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets), "AHB socket");
    out->pixmap = xcb_generate_id(c);
    // Termux:X11's existing DRI3 extension: 1255 carries an AHardwareBuffer socket.
    xcb_void_cookie_t import = xcb_dri3_pixmap_from_buffers_checked(c, out->pixmap,
        out->window, 1, desc.width, desc.height, desc.stride * 4, 0, 0, 0, 0, 0, 0, 0,
        24, 32, 1255, &sockets[1]);
    xcb_flush(c);
    require(!AHardwareBuffer_sendHandleToUnixSocket(out->buffer, sockets[0]), "AHB send");
    checked(c, import, "DRI3 AHB import");
    close(sockets[0]);
    // libxcb owns the FD passed in the DRI3 request.
    printf("AHB_BUFFER slot=%d xid=%u size=%dx%d stride=%u\n", slot, out->window,
        out->width, out->height, desc.stride);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    xcb_connection_t* c = xcb_connect(NULL, NULL);
    require(!xcb_connection_has_error(c), "X connection");
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    const xcb_query_extension_reply_t* present = xcb_get_extension_data(c, &xcb_present_id);
    require(present && present->present, "Present extension");
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    require(eglInitialize(display, NULL, NULL), "EGL init");
    EGLint configAttrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
    EGLConfig config;
    EGLint count;
    require(eglChooseConfig(display, configAttrs, &config, 1, &count) && count, "EGL config");
    EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLint surfaceAttrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
    EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttrs);
    require(eglMakeCurrent(display, surface, surface, context), "EGL current");
    printf("AHB_RENDERER=%s\n", glGetString(GL_RENDERER));
    Output outputs[2] = {0};
    for (int i = 0; i < 2; i++) {
        Output* out = &outputs[i];
        out->requestedWidth = 480;
        out->requestedHeight = 320;
        out->window = xcb_generate_id(c);
        uint32_t events = XCB_EVENT_MASK_STRUCTURE_NOTIFY;
        checked(c, xcb_create_window_checked(c, screen->root_depth, out->window, screen->root,
            32, 48, 480, 320, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
            XCB_CW_EVENT_MASK, &events), "create window");
        if (!getenv("X11_EXAMPLE_ROOT"))
            checked(c, xcb_composite_redirect_window_checked(c, out->window,
                XCB_COMPOSITE_REDIRECT_MANUAL), "redirect window");
        checked(c, xcb_present_select_input_checked(c, xcb_generate_id(c), out->window,
            XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY), "select Present idle");
        xcb_map_window(c, out->window);
        allocate_buffer(c, display, out, i);
        printf("AHB_WINDOW slot=%d xid=%u\n", i, out->window);
    }
    int timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    struct itimerspec interval = {{0, 33333333}, {0, 33333333}};
    require(timer >= 0 && !timerfd_settime(timer, 0, &interval, NULL), "frame timer");
    struct pollfd fds[] = {{xcb_get_file_descriptor(c), POLLIN, 0}, {timer, POLLIN, 0}};
    uint64_t frame = 0, submitted = 0, idle = 0;
    while (running && !xcb_connection_has_error(c)) {
        xcb_generic_event_t* event;
        while ((event = xcb_poll_for_event(c))) {
            uint8_t type = event->response_type & 127;
            if (type == XCB_GE_GENERIC) {
                xcb_present_idle_notify_event_t* e = (void*)event;
                if (e->extension == present->major_opcode && e->event_type == XCB_PRESENT_IDLE_NOTIFY)
                    for (int i = 0; i < 2; i++) if (outputs[i].window == e->window &&
                            outputs[i].pixmap == e->pixmap && outputs[i].serial == e->serial) {
                        outputs[i].busy = false;
                        idle++;
                    }
            } else if (type == XCB_CONFIGURE_NOTIFY) {
                xcb_configure_notify_event_t* e = (void*)event;
                for (int i = 0; i < 2; i++) if (outputs[i].window == e->window) {
                    outputs[i].requestedWidth = e->width;
                    outputs[i].requestedHeight = e->height;
                }
            } else if (!type) {
                fprintf(stderr, "Xerror=%u\n", ((xcb_generic_error_t*)event)->error_code);
                running = 0;
            }
            free(event);
        }
        if (poll(fds, 2, -1) <= 0) continue;
        if (fds[0].revents & (POLLHUP | POLLERR)) break;
        if (!(fds[1].revents & POLLIN)) continue;
        uint64_t ticks;
        if (read(timer, &ticks, sizeof(ticks)) != sizeof(ticks)) break;
        frame += ticks;
        for (int i = 0; i < 2; i++) {
            Output* out = &outputs[i];
            if (out->busy) continue;
            if (out->width != out->requestedWidth || out->height != out->requestedHeight)
                allocate_buffer(c, display, out, i);
            glBindFramebuffer(GL_FRAMEBUFFER, out->framebuffer);
            glViewport(0, 0, out->width, out->height);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(i ? .1f : .8f, .2f, i ? .8f : .1f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);
            glScissor((frame * 5) % out->width, out->height / 4, out->width / 5, out->height / 2);
            glClearColor(1, 1, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
            // Finish writes before Present; wait for IdleNotify before reusing this buffer.
            glFinish();
            xcb_present_pixmap(c, out->window, out->pixmap, ++out->serial,
                XCB_NONE, XCB_NONE, 0, 0, XCB_NONE, XCB_NONE, XCB_NONE,
                XCB_PRESENT_OPTION_COPY, 0, 0, 0, 0, NULL);
            out->busy = true;
            submitted++;
        }
        xcb_flush(c);
        if (frame % 150 == 0) printf("AHB_FRAME ticks=%llu submitted=%llu idle=%llu\n",
            (unsigned long long)frame, (unsigned long long)submitted, (unsigned long long)idle);
    }
    glFinish();
    for (int i = 0; i < 2; i++) {
        xcb_destroy_window(c, outputs[i].window);
        release_buffer(c, display, &outputs[i]);
    }
    xcb_flush(c);
    xcb_disconnect(c);
    close(timer);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
    printf("AHB_STOP submitted=%llu idle=%llu\n", (unsigned long long)submitted, (unsigned long long)idle);
    return 0;
}
