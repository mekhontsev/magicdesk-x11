#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>

/* Exercise an existing individual-window output without changing its owner. */
static xcb_connection_t* connection;
static xcb_screen_t* screen;
typedef struct { int x, y, width, height; } Bounds;

static xcb_atom_t atom(const char* name) {
    xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(connection,
            xcb_intern_atom(connection, 0, strlen(name), name), NULL);
    if (!reply) exit(1);
    xcb_atom_t result = reply->atom;
    free(reply);
    return result;
}

static Bounds geometry(xcb_window_t window) {
    xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(connection,
            xcb_get_geometry(connection, window), NULL);
    if (!reply) { fprintf(stderr, "Missing window %u\n", window); exit(1); }
    Bounds result = {reply->x, reply->y, reply->width, reply->height};
    free(reply);
    return result;
}

static int same(Bounds a, Bounds b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static long long now(void) {
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (long long)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}

static void expect(xcb_window_t window, Bounds wanted) {
    Bounds actual = geometry(window);
    if (!same(actual, wanted)) {
        fprintf(stderr, "Window %u: actual=%d,%d %dx%d expected=%d,%d %dx%d\n", window,
                actual.x, actual.y, actual.width, actual.height, wanted.x, wanted.y, wanted.width, wanted.height);
        exit(1);
    }
}

static void awaitGeometry(xcb_window_t window, Bounds wanted) {
    long long deadline = now() + 5000;
    while (!same(geometry(window), wanted)) {
        int received = 0;
        xcb_generic_event_t* event;
        while ((event = xcb_poll_for_event(connection))) { free(event); received = 1; }
        int remaining = (int)(deadline - now());
        if (remaining <= 0) { expect(window, wanted); return; }
        if (received) continue;
        // EVENT_WAIT: ConfigureNotify/MapNotify; expiry fails the fixture, never implies placement.
        struct pollfd fd = {xcb_get_file_descriptor(connection), POLLIN, 0};
        int result = poll(&fd, 1, remaining);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0 || (fd.revents & (POLLERR | POLLHUP))) { expect(window, wanted); return; }
    }
}

static xcb_window_t create(xcb_window_t parent, Bounds bounds, const char* type,
        uint32_t positionHint, int overrideRedirect) {
    xcb_window_t window = xcb_generate_id(connection);
    uint32_t values[] = {0x447766, overrideRedirect, XCB_EVENT_MASK_STRUCTURE_NOTIFY};
    xcb_create_window(connection, screen->root_depth, window, screen->root,
            bounds.x, bounds.y, bounds.width, bounds.height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
            screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, values);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_TRANSIENT_FOR,
            XCB_ATOM_WINDOW, 32, 1, &parent);
    xcb_atom_t kind = atom(type);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, atom("_NET_WM_WINDOW_TYPE"),
            XCB_ATOM_ATOM, 32, 1, &kind);
    uint32_t hints[18] = {[0] = positionHint | (1u << 4) | (1u << 5),
            [5] = bounds.width, [6] = bounds.height, [7] = bounds.width, [8] = bounds.height};
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NORMAL_HINTS,
            XCB_ATOM_WM_SIZE_HINTS, 32, 18, hints);
    xcb_map_window(connection, window);
    xcb_flush(connection);
    return window;
}

static Bounds centered(Bounds parent, int width, int height) {
    return (Bounds){parent.x + (parent.width - width) / 2,
            parent.y + (parent.height - height) / 2, width, height};
}

static void barrier(xcb_window_t parent, Bounds bounds) {
    Bounds expected = centered(bounds, 55, 33);
    xcb_window_t sentinel = create(parent, (Bounds){0,0,55,33}, "_NET_WM_WINDOW_TYPE_DIALOG", 0, 0);
    awaitGeometry(sentinel, expected);
    xcb_destroy_window(connection, sentinel);
    xcb_flush(connection);
}

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "dialog-placement HOSTED_PARENT_XID\n"); return 2; }
    connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection)) return 1;
    screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    xcb_window_t parent = strtoul(argv[1], NULL, 0);
    Bounds owner = geometry(parent);
    if (owner.width < 500 || owner.height < 300) return 2;
    const char* dialog = "_NET_WM_WINDOW_TYPE_DIALOG";
    Bounds expected = centered(owner, 200, 120);
    xcb_window_t automatic = create(parent, (Bounds){0,0,200,120}, dialog, 0, 0);
    awaitGeometry(automatic, expected);
    Bounds positioned = {owner.x + 20, owner.y + 30, 150, 80};
    xcb_window_t program = create(parent, positioned, dialog, 1u << 2, 0);
    xcb_window_t user = create(parent, positioned, dialog, 1u << 0, 0);
    xcb_window_t menu = create(parent, positioned, "_NET_WM_WINDOW_TYPE_POPUP_MENU", 0, 0);
    xcb_window_t tooltip = create(parent, positioned, "_NET_WM_WINDOW_TYPE_TOOLTIP", 0, 0);
    xcb_window_t bypass = create(parent, positioned, dialog, 0, 1);
    xcb_window_t nested = create(automatic, (Bounds){0,0,60,40}, dialog, 0, 0);
    awaitGeometry(nested, centered(expected, 60, 40));
    barrier(parent, owner);
    expect(program, positioned); expect(user, positioned); expect(menu, positioned);
    expect(tooltip, positioned); expect(bypass, positioned);
    xcb_destroy_window(connection, nested);
    uint32_t moved[] = {owner.x + 50, owner.y + 60};
    xcb_configure_window(connection, automatic, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, moved);
    barrier(parent, owner);
    Bounds manual = {moved[0], moved[1], 200, 120};
    expect(automatic, manual);
    xcb_unmap_window(connection, automatic);
    xcb_map_window(connection, automatic);
    barrier(parent, owner);
    expect(automatic, manual);
    xcb_disconnect(connection);
    puts("Dialog placement: center, nested parent, explicit positions, menu, tooltip, override, move and remap passed");
    return 0;
}
