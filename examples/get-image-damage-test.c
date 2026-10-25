#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/damage.h>

/* SourceValidate paints redirected children and may emit DamageNotify. Those
 * events must precede the GetImage reply, never split its header and pixels. */
int main(void)
{
    alarm(20);
    xcb_auth_info_t auth = {0};
    unsigned char cookie[16];
    const char *hex = getenv("X11_TEST_COOKIE");
    if (hex) {
        assert(strlen(hex) == sizeof(cookie) * 2);
        for (unsigned i = 0; i < sizeof(cookie); ++i) {
            unsigned byte;
            assert(sscanf(hex + i * 2, "%2x", &byte) == 1);
            cookie[i] = byte;
        }
        auth = (xcb_auth_info_t){18, "MIT-MAGIC-COOKIE-1", sizeof(cookie), (char *)cookie};
    }
    xcb_connection_t *c = xcb_connect_to_display_with_auth_info(NULL, hex ? &auth : NULL, NULL);
    assert(!xcb_connection_has_error(c));
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    assert(screen && screen->root_depth == 24);
    const xcb_query_extension_reply_t *damage_ext = xcb_get_extension_data(c, &xcb_damage_id);
    assert(damage_ext && damage_ext->present);
    free(xcb_composite_query_version_reply(c, xcb_composite_query_version(c, 0, 4), NULL));
    free(xcb_damage_query_version_reply(c, xcb_damage_query_version(c, 1, 1), NULL));
    xcb_window_t parent = xcb_generate_id(c), child = xcb_generate_id(c);
    uint32_t values[] = {screen->black_pixel, 1};
    xcb_create_window(c, screen->root_depth, parent, screen->root, 20, 20, 128, 512,
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT, values);
    xcb_create_window(c, screen->root_depth, child, parent, 0, 0, 128, 512,
                      0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, NULL);
    xcb_composite_redirect_window(c, child, XCB_COMPOSITE_REDIRECT_AUTOMATIC);
    xcb_map_window(c, child);
    xcb_map_window(c, parent);
    xcb_damage_damage_t damage = xcb_generate_id(c);
    xcb_damage_create(c, damage, parent, XCB_DAMAGE_REPORT_LEVEL_RAW_RECTANGLES);
    xcb_gcontext_t gc = xcb_generate_id(c);
    xcb_create_gc(c, gc, child, 0, NULL);
    xcb_rectangle_t rect = {0, 0, 128, 512};
    unsigned events = 0;
    for (unsigned round = 0; round < 16; ++round) {
        uint32_t color = round & 1 ? 0 : 0xffffff;
        uint8_t format = round & 2 ? XCB_IMAGE_FORMAT_XY_PIXMAP : XCB_IMAGE_FORMAT_Z_PIXMAP;
        unsigned height = round & 4 ? 512 : 64;
        xcb_change_gc(c, gc, XCB_GC_FOREGROUND, &color);
        xcb_poly_fill_rectangle(c, child, gc, 1, &rect);
        xcb_generic_error_t *error = NULL;
        xcb_get_image_reply_t *reply = xcb_get_image_reply(c,
            xcb_get_image(c, format, parent, 0, 0, 128, height, 0xffffff), &error);
        assert(!error && reply);
        size_t size = xcb_get_image_data_length(reply);
        unsigned char *pixels = xcb_get_image_data(reply);
        unsigned char expected = color ? 255 : 0;
        assert(size == 128 * height * (format == XCB_IMAGE_FORMAT_Z_PIXMAP ? 4 : 3));
        for (size_t i = 0; i < size; ++i) {
            if (format == XCB_IMAGE_FORMAT_Z_PIXMAP && (i & 3) == 3)
                continue;
            if (pixels[i] != expected) {
                fprintf(stderr, "FAIL GetImage payload round=%u offset=%zu actual=%02x expected=%02x\n",
                        round, i, pixels[i], expected);
                return 1;
            }
        }
        free(reply);
        xcb_generic_event_t *event;
        while ((event = xcb_poll_for_event(c))) {
            assert((event->response_type & 127) == damage_ext->first_event + XCB_DAMAGE_NOTIFY);
            ++events;
            free(event);
        }
        assert(!xcb_connection_has_error(c));
    }
    assert(events >= 16);
    xcb_destroy_window(c, parent);
    xcb_flush(c);
    xcb_disconnect(c);
    printf("PASS GetImage/Damage wire ordering: 16 ZPixmap/XYPixmap replies, %u events\n", events);
    return 0;
}
