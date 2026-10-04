#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <assert.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void checked(xcb_connection_t *c, xcb_void_cookie_t cookie) {
    xcb_generic_error_t *error = xcb_request_check(c, cookie);
    if (error) { fprintf(stderr, "X error=%u request=%u minor=%u\n", error->error_code,
                        error->major_code, error->minor_code); exit(1); }
}

static void syncBuffer(int fd, uint64_t flags) {
    struct dma_buf_sync sync = {.flags = flags};
    assert(ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) == 0);
}

static int exportBuffer(xcb_connection_t *c, xcb_pixmap_t pixmap,
                        uint32_t *stride, uint32_t *offset) {
    xcb_generic_error_t *error = NULL;
    xcb_dri3_buffers_from_pixmap_reply_t *reply = xcb_dri3_buffers_from_pixmap_reply(c,
        xcb_dri3_buffers_from_pixmap(c, pixmap), &error);
    if (!reply || error) {
        fprintf(stderr, "DRI3 export failed: X error=%u\n", error ? error->error_code : 0);
        exit(1);
    }
    assert(reply->nfd == 1 && reply->modifier == 0 && reply->bpp == 32);
    *stride = *xcb_dri3_buffers_from_pixmap_strides(reply);
    *offset = *xcb_dri3_buffers_from_pixmap_offsets(reply);
    int fd = *xcb_dri3_buffers_from_pixmap_reply_fds(c, reply);
    free(reply);
    return fd;
}

static void exercise(xcb_connection_t *c, xcb_screen_t *screen, uint8_t depth) {
    const unsigned width = 37, height = 23;
    xcb_pixmap_t pixmap = xcb_generate_id(c);
    checked(c, xcb_create_pixmap_checked(c, depth, pixmap, screen->root, width, height));
    xcb_gcontext_t gc = xcb_generate_id(c);
    uint32_t color = 0x791155aa;
    checked(c, xcb_create_gc_checked(c, gc, pixmap, XCB_GC_FOREGROUND, &color));
    xcb_rectangle_t rectangle = {0, 0, width, height};
    checked(c, xcb_poly_fill_rectangle_checked(c, pixmap, gc, 1, &rectangle));
    uint32_t stride, offset;
    int fd = exportBuffer(c, pixmap, &stride, &offset);
    assert(stride >= width * 4);
    size_t size = offset + (size_t)stride * height;
    unsigned char *pixels = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    assert(pixels != MAP_FAILED);
    uint32_t mask = depth == 24 ? 0xffffff : UINT32_MAX;
    syncBuffer(fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
    assert((*(uint32_t *)(pixels + offset) & mask) == (color & mask));
    assert((*(uint32_t *)(pixels + offset + (height - 1) * stride + (width - 1) * 4) & mask)
           == (color & mask));
    syncBuffer(fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);

    color = 0xbdcc7722;
    checked(c, xcb_change_gc_checked(c, gc, XCB_GC_FOREGROUND, &color));
    rectangle = (xcb_rectangle_t){17, 11, width - 17, height - 11};
    checked(c, xcb_poly_fill_rectangle_checked(c, pixmap, gc, 1, &rectangle));
    syncBuffer(fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            uint32_t expected = x >= 17 && y >= 11 ? color : 0x791155aa;
            assert((*(uint32_t *)(pixels + offset + y * stride + x * 4) & mask) == (expected & mask));
        }
    syncBuffer(fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);

    xcb_get_image_reply_t *readback = xcb_get_image_reply(c,
        xcb_get_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, 0, 0, width, height, UINT32_MAX), NULL);
    assert(readback && xcb_get_image_data_length(readback) == width * height * 4);
    uint32_t *image = (uint32_t *)xcb_get_image_data(readback);
    assert((image[width * height - 1] & mask) == (color & mask));
    assert((image[0] & mask) == (0x791155aa & mask));
    free(readback);

    xcb_generic_error_t *error = NULL;
    xcb_dri3_buffer_from_pixmap_reply_t *legacy = xcb_dri3_buffer_from_pixmap_reply(c,
        xcb_dri3_buffer_from_pixmap(c, pixmap), &error);
    assert(legacy && !error && legacy->nfd == 1 && legacy->stride == stride && offset == 0);
    int retained = *xcb_dri3_buffer_from_pixmap_reply_fds(c, legacy);
    struct stat a, b;
    assert(!fstat(fd, &a) && !fstat(retained, &b) && a.st_dev == b.st_dev && a.st_ino == b.st_ino);
    assert(legacy->size == (uint64_t)a.st_size && legacy->size >= size);
    free(legacy);

    xcb_pixmap_t imported = xcb_generate_id(c);
    int importFd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    assert(importFd >= 0);
    checked(c, xcb_dri3_pixmap_from_buffers_checked(c, imported, screen->root, 1,
        width, height, stride, offset, 0, 0, 0, 0, 0, 0, depth, 32, 0, &importFd));
    uint32_t returnedStride, returnedOffset;
    int returned = exportBuffer(c, imported, &returnedStride, &returnedOffset);
    assert(returnedStride == stride && returnedOffset == offset);
    assert(!fstat(returned, &b) && a.st_ino == b.st_ino && a.st_dev == b.st_dev);
    close(returned);
    checked(c, xcb_free_pixmap_checked(c, imported));
    importFd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    assert(importFd >= 0);
    checked(c, xcb_dri3_pixmap_from_buffers_checked(c, imported, screen->root, 1,
        width - 1, height - 1, stride, 4, 0, 0, 0, 0, 0, 0, depth, 32, 0, &importFd));
    returned = exportBuffer(c, imported, &returnedStride, &returnedOffset);
    assert(returnedStride == stride && returnedOffset == 4);
    close(returned);
    legacy = xcb_dri3_buffer_from_pixmap_reply(c, xcb_dri3_buffer_from_pixmap(c, imported), &error);
    assert(!legacy && error && error->error_code == XCB_PIXMAP);
    free(error);
    checked(c, xcb_free_pixmap_checked(c, imported));
    checked(c, xcb_free_pixmap_checked(c, pixmap));
    checked(c, xcb_free_gc_checked(c, gc));
    close(fd);
    syncBuffer(retained, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
    assert((*(uint32_t *)(pixels + offset + (height - 1) * stride + (width - 1) * 4) & mask)
           == (color & mask));
    syncBuffer(retained, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
    munmap(pixels, size);
    close(retained);
    printf("PASS depth=%u: live pixels, both DRI3 exports, reimport and retained FD\n", depth);
}

int main(void) {
    xcb_connection_t *c = xcb_connect(NULL, NULL);
    assert(!xcb_connection_has_error(c));
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    xcb_dri3_query_version_reply_t *version = xcb_dri3_query_version_reply(c,
        xcb_dri3_query_version(c, 1, 2), NULL);
    assert(version && version->major_version == 1 && version->minor_version >= 2);
    free(version);
    exercise(c, screen, 24);
    exercise(c, screen, 32);
    xcb_pixmap_t wide = xcb_generate_id(c);
    checked(c, xcb_create_pixmap_checked(c, 24, wide, screen->root, 16384, 1));
    uint32_t wideStride, wideOffset;
    int wideFd = exportBuffer(c, wide, &wideStride, &wideOffset);
    assert(wideStride > UINT16_MAX && wideOffset == 0);
    close(wideFd);
    xcb_generic_error_t *error = NULL;
    xcb_dri3_buffer_from_pixmap_reply_t *legacy = xcb_dri3_buffer_from_pixmap_reply(c,
        xcb_dri3_buffer_from_pixmap(c, wide), &error);
    assert(!legacy && error && error->error_code == XCB_PIXMAP);
    free(error);
    checked(c, xcb_free_pixmap_checked(c, wide));
    puts("PASS legacy DRI3 rejects unrepresentable pitch and offsets");
    xcb_pixmap_t mono = xcb_generate_id(c);
    checked(c, xcb_create_pixmap_checked(c, 1, mono, screen->root, 8, 8));
    error = NULL;
    xcb_dri3_buffers_from_pixmap_reply_t *reply = xcb_dri3_buffers_from_pixmap_reply(c,
        xcb_dri3_buffers_from_pixmap(c, mono), &error);
    assert(!reply && error && error->error_code == XCB_PIXMAP);
    free(error);
    checked(c, xcb_free_pixmap_checked(c, mono));
    xcb_disconnect(c);
    puts("PASS unsupported depth rejected without disconnecting X client");
    return 0;
}
