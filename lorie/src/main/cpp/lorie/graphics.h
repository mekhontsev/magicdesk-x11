#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct AHardwareBuffer AHardwareBuffer;
typedef struct ANativeWindow ANativeWindow;
typedef struct {
    void *image;
    float sx, sy, sw, sh, x, y, width, height;
    int clipX, clipY, clipWidth, clipHeight;
    bool blend, linear, swapRedBlue;
} LorieGraphicsDraw;

/* Immutable host-owned operations. Handles belong to one renderer thread.
 * submit waits for pixel access completion; present may wait for Android and
 * must run outside the shared X pixel mutex. No Java or X protocol objects. */
typedef struct {
    void *(*create)(void);
    void (*destroy)(void *device);
    void *(*image)(void *device, AHardwareBuffer *buffer, const void *pixels,
        unsigned width, unsigned height, size_t stride, unsigned androidFormat);
    void (*releaseImage)(void *image);
    void *(*surface)(void *device, ANativeWindow *window);
    void (*releaseSurface)(void *surface);
    void *(*acquire)(void *surface, unsigned width, unsigned height);
    void *(*begin)(void *device, void *target, const float clear[4], bool preserve);
    bool (*draw)(void *pass, const LorieGraphicsDraw *draw);
    bool (*submit)(void *pass);
    void (*cancel)(void *pass);
    bool (*present)(void *surface);
} LorieGraphics;
