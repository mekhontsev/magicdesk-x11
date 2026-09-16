#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

static volatile sig_atomic_t running = 1;
static void stop(int signal) { (void) signal; running = 0; }

static xcb_atom_t atom(xcb_connection_t* connection, const char* name) {
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(connection,
            xcb_intern_atom(connection, 0, strlen(name), name), NULL);
    xcb_atom_t result = reply ? reply->atom : XCB_NONE;
    free(reply);
    return result;
}

// Start in an unreachable saved position, then observe real input from the Android host.
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    xcb_connection_t* connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection)) return 1;
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    xcb_window_t window = xcb_generate_id(connection);
    uint32_t values[] = {0x247040, XCB_EVENT_MASK_STRUCTURE_NOTIFY
            | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION};
    xcb_create_window(connection, screen->root_depth, window, screen->root,
            -80, -1200, 320, 240, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
            XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
    const char* title = "X11 input geometry";
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME,
            XCB_ATOM_STRING, 8, strlen(title), title);
    const uint32_t icon[] = {2, 2, 0xff00ff00, 0xff0000ff, 0xffff0000, 0xffffff00};
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, atom(connection, "_NET_WM_ICON"),
            XCB_ATOM_CARDINAL, 32, sizeof(icon) / sizeof(icon[0]), icon);
    xcb_atom_t protocols = atom(connection, "WM_PROTOCOLS"), close = atom(connection, "WM_DELETE_WINDOW");
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, protocols, XCB_ATOM_ATOM, 32, 1, &close);
    xcb_map_window(connection, window);
    xcb_flush(connection);
    printf("WINDOW xid=%u initial=-80,-1200\n", window);
    struct pollfd fd = {xcb_get_file_descriptor(connection), POLLIN, 0};
    while (running && !xcb_connection_has_error(connection)) {
        xcb_generic_event_t* event;
        while ((event = xcb_poll_for_event(connection))) {
            int type = event->response_type & 0x7f;
            if (type == XCB_CONFIGURE_NOTIFY) {
                xcb_configure_notify_event_t* e = (void*) event;
                printf("GEOMETRY %d,%d %ux%u\n", e->x, e->y, e->width, e->height);
            } else if (type == XCB_BUTTON_PRESS || type == XCB_BUTTON_RELEASE || type == XCB_MOTION_NOTIFY) {
                xcb_button_press_event_t* e = (void*) event;
                printf("INPUT type=%d button=%u local=%d,%d root=%d,%d\n",
                        type, e->detail, e->event_x, e->event_y, e->root_x, e->root_y);
            } else if (type == XCB_CLIENT_MESSAGE) {
                xcb_client_message_event_t* e = (void*) event;
                if (e->type == protocols && e->data.data32[0] == close) running = 0;
            } else if (!type) {
                fprintf(stderr, "X error %u\n", ((xcb_generic_error_t*) event)->error_code);
                running = 0;
            }
            free(event);
        }
        if (!running || poll(&fd, 1, -1) < 0 || (fd.revents & (POLLHUP | POLLERR))) break;
    }
    xcb_disconnect(connection);
    return 0;
}
