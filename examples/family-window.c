#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

static xcb_connection_t* connection;
static xcb_screen_t* screen;
static xcb_window_t mainWindow, popup;

static void fixedSize(xcb_window_t window) {
    uint32_t hints[18] = {[0] = (1u << 4) | (1u << 5),
            [5] = 500, [6] = 300, [7] = 500, [8] = 300};
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
            XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32, 18, hints);
}

static void tiledBackground(xcb_window_t window) {
    xcb_pixmap_t pixmap = xcb_generate_id(connection);
    xcb_gcontext_t gc = xcb_generate_id(connection);
    xcb_create_pixmap(connection, screen->root_depth, pixmap, window, 500, 300);
    uint32_t color = 0x264e73;
    xcb_create_gc(connection, gc, pixmap, XCB_GC_FOREGROUND, &color);
    xcb_rectangle_t background = {0, 0, 500, 300};
    xcb_poly_fill_rectangle(connection, pixmap, gc, 1, &background);
    color = 0xe4b149;
    xcb_change_gc(connection, gc, XCB_GC_FOREGROUND, &color);
    xcb_rectangle_t markers[] = {{20, 20, 140, 80}, {340, 200, 140, 80}};
    xcb_poly_fill_rectangle(connection, pixmap, gc, 2, markers);
    xcb_change_window_attributes(connection, window, XCB_CW_BACK_PIXMAP, &pixmap);
    xcb_free_gc(connection, gc);
    xcb_free_pixmap(connection, pixmap);
}

static xcb_atom_t atom(const char* name) {
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(connection,
            xcb_intern_atom(connection, 0, strlen(name), name), NULL);
    xcb_atom_t result = reply ? reply->atom : XCB_NONE;
    free(reply);
    return result;
}

static xcb_window_t create(const char* name, int x, int y, int width, int height,
        uint32_t color, const char* type, xcb_window_t parent) {
    xcb_window_t id = xcb_generate_id(connection);
    uint32_t values[] = {color, XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_KEY_PRESS};
    xcb_create_window(connection, screen->root_depth, id, screen->root, x, y, width, height, 0,
            XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, id, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(name), name);
    if (type) {
        xcb_atom_t value = atom(type);
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, id, atom("_NET_WM_WINDOW_TYPE"), XCB_ATOM_ATOM, 32, 1, &value);
    }
    if (parent) xcb_change_property(connection, XCB_PROP_MODE_REPLACE, id,
            XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1, &parent);
    return id;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection)) return 1;
    screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    xcb_window_t splash = create("Startup: press Enter", 10, 20, 500, 300, 0x264e73,
            argc > 1 && !strcmp(argv[1], "splash") ? "_NET_WM_WINDOW_TYPE_SPLASH" : NULL, 0);
    if (argc > 1 && !strcmp(argv[1], "fixed")) {
        fixedSize(splash);
        tiledBackground(splash);
    }
    xcb_map_window(connection, splash);
    xcb_flush(connection);
    printf("SPLASH %u\n", splash);
    struct pollfd fd = {xcb_get_file_descriptor(connection), POLLIN, 0};
    while (!xcb_connection_has_error(connection)) {
        xcb_generic_event_t* event;
        while ((event = xcb_poll_for_event(connection))) {
            int type = event->response_type & 0x7f;
            if (type == XCB_KEY_PRESS) {
                xcb_key_press_event_t* key = (void*)event;
                printf("KEY %u code=%u\n", key->event, key->detail);
                if (!mainWindow && key->detail == 69) {
                    xcb_delete_property(connection, splash, XCB_ATOM_WM_NORMAL_HINTS);
                } else if (!mainWindow && key->detail == 70) {
                    fixedSize(splash);
                } else if (!mainWindow && key->detail == 36) {
                    mainWindow = create("Family fixture: F1 dialog, F2 menu", 0, 0, 600, 400,
                            0x254c3a, "_NET_WM_WINDOW_TYPE_NORMAL", 0);
                    if (argc < 2 || strcmp(argv[1], "gap")) xcb_map_window(connection, mainWindow);
                    xcb_destroy_window(connection, splash);
                    printf("MAIN %u\n", mainWindow);
                } else if (mainWindow && (key->detail == 67 || key->detail == 68)) {
                    if (popup) xcb_destroy_window(connection, popup);
                    xcb_get_geometry_reply_t* geometry = xcb_get_geometry_reply(connection,
                            xcb_get_geometry(connection, mainWindow), NULL);
                    if (geometry) {
                        int menu = key->detail == 68;
                        popup = create(menu ? "Menu" : "Dialog", geometry->x + geometry->width - 20,
                                geometry->y + geometry->height - 30, menu ? 400 : geometry->width + 300,
                                menu ? 350 : 700, menu ? 0xbc8031 : 0x934267,
                                menu ? "_NET_WM_WINDOW_TYPE_POPUP_MENU" : "_NET_WM_WINDOW_TYPE_DIALOG", mainWindow);
                        xcb_map_window(connection, popup);
                        // Toolkits commonly focus a hidden real child rather than the
                        // transient itself. Host activation must preserve that focus.
                        xcb_window_t input = xcb_generate_id(connection);
                        xcb_create_window(connection, 0, input, popup, -1, -1, 1, 1, 0,
                                XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, 0, NULL);
                        xcb_map_window(connection, input);
                        xcb_set_input_focus(connection, XCB_INPUT_FOCUS_PARENT, input, XCB_CURRENT_TIME);
                        printf("FOCUS_CHILD %u parent=%u\n", input, popup);
                        printf("POPUP %u menu=%d\n", popup, menu);
                        free(geometry);
                    }
                }
            } else if (type == XCB_CONFIGURE_NOTIFY) {
                xcb_configure_notify_event_t* e = (void*)event;
                printf("GEOMETRY %u %d,%d %ux%u\n", e->window, e->x, e->y, e->width, e->height);
            } else if (type == XCB_BUTTON_PRESS) {
                xcb_button_press_event_t* e = (void*)event;
                printf("CLICK %u %d,%d\n", e->event, e->event_x, e->event_y);
                if (e->event == popup) { xcb_destroy_window(connection, popup); popup = 0; }
            } else if (!type) fprintf(stderr, "X ERROR %u\n", ((xcb_generic_error_t*)event)->error_code);
            free(event);
            xcb_flush(connection);
        }
        if (poll(&fd, 1, -1) < 0 || (fd.revents & (POLLERR | POLLHUP))) break;
    }
    xcb_disconnect(connection);
    return 0;
}
