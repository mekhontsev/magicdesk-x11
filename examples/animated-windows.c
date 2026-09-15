#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/composite.h>

static volatile sig_atomic_t running = 1;
static void stop(int signal) { (void) signal; running = 0; }

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    xcb_connection_t *c = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(c)) { fprintf(stderr, "X connection failed\n"); return 1; }
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    xcb_window_t windows[2];
    xcb_gcontext_t gc = xcb_generate_id(c);
    xcb_create_gc(c, gc, screen->root, 0, NULL);
    uint16_t width[2] = {320, 320}, height[2] = {240, 240};
    uint32_t colors[2] = {0xe03050, 0x2060e0};
    int alive[2] = {1, 1};
    int redirect = getenv("X11_EXAMPLE_REDIRECT") != NULL;
    if (redirect) {
        free(xcb_composite_query_version_reply(c, xcb_composite_query_version(c, 0, 4), NULL));
    }
    for (int i = 0; i < 2; i++) {
        windows[i] = xcb_generate_id(c);
        uint32_t values[] = {colors[i], XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
                | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_KEY_PRESS};
        xcb_create_window(c, screen->root_depth, windows[i], screen->root,
                32 + i * 350, 48, width[i], height[i], 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
        const char *name = i ? "X11 Example Blue" : "X11 Example Red";
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, windows[i], XCB_ATOM_WM_NAME,
                XCB_ATOM_STRING, 8, strlen(name), name);
        if (redirect) {
            xcb_generic_error_t *error = xcb_request_check(c,
                    xcb_composite_redirect_window_checked(c, windows[i], XCB_COMPOSITE_REDIRECT_MANUAL));
            if (error) { fprintf(stderr, "Composite error %u\n", error->error_code); return 1; }
        }
        xcb_map_window(c, windows[i]);
        printf("WINDOW slot=%d xid=%u color=%06x redirected=%d\n", i, windows[i], colors[i], redirect);
    }
    xcb_flush(c);
    int timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    struct itimerspec frame = {{0, 33333333}, {0, 33333333}};
    if (timer < 0 || timerfd_settime(timer, 0, &frame, NULL)) { perror("timerfd"); return 1; }
    struct pollfd fds[] = {{xcb_get_file_descriptor(c), POLLIN, 0}, {timer, POLLIN, 0}};
    uint64_t ticks = 0;
    while (running && !xcb_connection_has_error(c)) {
        xcb_generic_event_t *event;
        while ((event = xcb_poll_for_event(c))) {
            int type = event->response_type & 0x7f;
            if (type == XCB_CONFIGURE_NOTIFY) {
                xcb_configure_notify_event_t *e = (void *) event;
                for (int i = 0; i < 2; i++) if (windows[i] == e->window) {
                    width[i] = e->width; height[i] = e->height;
                    printf("RESIZE slot=%d width=%u height=%u\n", i, width[i], height[i]);
                }
            } else if (type == XCB_DESTROY_NOTIFY) {
                xcb_destroy_notify_event_t *e = (void *)event;
                for (int i = 0; i < 2; i++) if (windows[i] == e->window) {
                    alive[i] = 0;
                    printf("DESTROY slot=%d\n", i);
                }
            } else if (type == XCB_KEY_PRESS || type == XCB_BUTTON_PRESS) {
                xcb_key_press_event_t *e = (void *) event;
                printf("INPUT window=%u type=%d detail=%u x=%d y=%d\n",
                        e->event, type, e->detail, e->event_x, e->event_y);
                xcb_set_input_focus(c, XCB_INPUT_FOCUS_POINTER_ROOT, e->event, XCB_CURRENT_TIME);
            } else if (!type) {
                fprintf(stderr, "X error %u\n", ((xcb_generic_error_t *)event)->error_code);
                running = 0;
            }
            free(event);
        }
        if (poll(fds, 2, -1) <= 0) continue;
        if (fds[0].revents & (POLLHUP | POLLERR)) break;
        if (!(fds[1].revents & POLLIN)) continue;
        uint64_t count;
        if (read(timer, &count, sizeof(count)) != sizeof(count)) break;
        ticks += count;
        for (int i = 0; i < 2; i++) {
            if (!alive[i]) continue;
            xcb_change_gc(c, gc, XCB_GC_FOREGROUND, &colors[i]);
            xcb_rectangle_t background = {0, 0, width[i], height[i]};
            xcb_poly_fill_rectangle(c, windows[i], gc, 1, &background);
            uint32_t white = 0xffffff;
            xcb_change_gc(c, gc, XCB_GC_FOREGROUND, &white);
            xcb_rectangle_t marker = {(int16_t) (ticks * 3 % (width[i] > 40 ? width[i] - 40 : 1)), 70, 32, 32};
            xcb_poly_fill_rectangle(c, windows[i], gc, 1, &marker);
            char text[80];
            int len = snprintf(text, sizeof(text), "%s frame %llu", i ? "BLUE" : "RED", (unsigned long long)ticks);
            xcb_image_text_8(c, len, windows[i], gc, 10, 25, text);
        }
        xcb_flush(c);
    }
    for (int i = 0; i < 2; i++) if (alive[i]) xcb_destroy_window(c, windows[i]);
    xcb_flush(c);
    close(timer);
    xcb_disconnect(c);
    printf("STOP frames=%llu\n", (unsigned long long)ticks);
    return 0;
}
