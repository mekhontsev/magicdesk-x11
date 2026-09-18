#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

static xcb_atom_t atom(xcb_connection_t* c, const char* name) {
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, strlen(name), name), NULL);
    xcb_atom_t result = reply ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return result;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "control-window XID info|map|unmap|destroy|raise|focus|geometry [x y width height]|fullscreen [0|1|2]\n"); return 2; }
    xcb_connection_t* c = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(c)) return 1;
    xcb_window_t window = strtoul(argv[1], NULL, 0);
    if (!strcmp(argv[2], "info")) {
        xcb_get_geometry_reply_t* geometry = xcb_get_geometry_reply(c, xcb_get_geometry(c, window), NULL);
        if (geometry) printf("geometry=%d,%d %ux%u\n", geometry->x, geometry->y, geometry->width, geometry->height);
        free(geometry);
        xcb_get_property_reply_t* hints = xcb_get_property_reply(c,
                xcb_get_property(c, 0, window, XCB_ATOM_WM_NORMAL_HINTS, XCB_GET_PROPERTY_TYPE_ANY, 0, 18), NULL);
        if (hints && hints->format == 32 && hints->value_len >= 7) {
            uint32_t* values = xcb_get_property_value(hints);
            printf("normal-hints flags=%u min=%ux%u\n", values[0], values[5], values[6]);
        }
        free(hints);
        xcb_get_input_focus_reply_t* focus = xcb_get_input_focus_reply(c, xcb_get_input_focus(c), NULL);
        if (focus) printf("focus=%u\n", focus->focus);
        free(focus);
        xcb_query_keymap_reply_t* keys = xcb_query_keymap_reply(c, xcb_query_keymap(c), NULL);
        if (keys) {
            printf("pressed=");
            for (unsigned key = 8; key < 256; key++) if (keys->keys[key / 8] & (1u << (key % 8))) printf(" %u", key);
            printf("\n");
        }
        free(keys);
        xcb_disconnect(c);
        return 0;
    }
    xcb_void_cookie_t command;
    if (!strcmp(argv[2], "map")) command = xcb_map_window_checked(c, window);
    else if (!strcmp(argv[2], "unmap")) command = xcb_unmap_window_checked(c, window);
    else if (!strcmp(argv[2], "destroy")) command = xcb_destroy_window_checked(c, window);
    else if (!strcmp(argv[2], "raise")) {
        uint32_t above = XCB_STACK_MODE_ABOVE;
        command = xcb_configure_window_checked(c, window, XCB_CONFIG_WINDOW_STACK_MODE, &above);
    }
    else if (!strcmp(argv[2], "focus")) command = xcb_set_input_focus_checked(c, XCB_INPUT_FOCUS_PARENT, window, XCB_CURRENT_TIME);
    else if (!strcmp(argv[2], "fullscreen") && argc == 4) {
        int action = atoi(argv[3]);
        if (action < 0 || action > 2) return 2;
        xcb_client_message_event_t event = {.response_type = XCB_CLIENT_MESSAGE, .format = 32,
                .window = window, .type = atom(c, "_NET_WM_STATE")};
        event.data.data32[0] = action;
        event.data.data32[1] = atom(c, "_NET_WM_STATE_FULLSCREEN");
        event.data.data32[3] = 1;
        xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
        command = xcb_send_event_checked(c, 0, root,
                XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (const char*)&event);
    }
    else if (!strcmp(argv[2], "geometry") && argc == 7) {
        uint32_t values[] = {atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), atoi(argv[6])};
        command = xcb_configure_window_checked(c, window,
                XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
    } else { xcb_disconnect(c); return 2; }
    xcb_generic_error_t* error = xcb_request_check(c, command);
    if (error) { fprintf(stderr, "X error %u\n", error->error_code); free(error); xcb_disconnect(c); return 1; }
    printf("window=%u operation=%s accepted\n", window, argv[2]);
    xcb_disconnect(c);
    return 0;
}
