#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "control-window XID map|unmap|destroy|geometry [x y width height]\n"); return 2; }
    xcb_connection_t* c = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(c)) return 1;
    xcb_window_t window = strtoul(argv[1], NULL, 0);
    xcb_void_cookie_t command;
    if (!strcmp(argv[2], "map")) command = xcb_map_window_checked(c, window);
    else if (!strcmp(argv[2], "unmap")) command = xcb_unmap_window_checked(c, window);
    else if (!strcmp(argv[2], "destroy")) command = xcb_destroy_window_checked(c, window);
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
