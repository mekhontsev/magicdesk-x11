#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xcursor/Xcursor.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Test client only: core cursors, transparent/ARGB images and server animation.
int main(void) {
    Display* display = XOpenDisplay(NULL);
    assert(display);
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);
    enum { PANE_COUNT = 7 };
    Window window = XCreateSimpleWindow(display, root, 0, 0, 150 * PANE_COUNT, 350, 0, 0, 0xffffff);
    XStoreName(display, window, "X11 cursor shapes");
    XClassHint hint = {.res_name = "magicdesk-cursor-test", .res_class = "MagicDeskCursorTest"};
    XSetClassHint(display, window, &hint);
    Atom close = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &close, 1);
    XSelectInput(display, window, StructureNotifyMask | KeyPressMask);
    const char* labels[] = {"Arrow", "Text", "Link", "Hidden", "Animated ARGB", "Inherited", "Explicit X"};
    Cursor cursors[PANE_COUNT] = {
        XCreateFontCursor(display, XC_left_ptr),
        XCreateFontCursor(display, XC_xterm),
        XCreateFontCursor(display, XC_hand2)
    };
    XcursorImage* hidden = XcursorImageCreate(1, 1);
    assert(hidden);
    hidden->xhot = hidden->yhot = 0;
    hidden->pixels[0] = 0;
    cursors[3] = XcursorImageLoadCursor(display, hidden);
    XcursorImageDestroy(hidden);
    XcursorImages* frames = XcursorImagesCreate(4);
    assert(frames);
    for (int i = 0; i < 4; i++) {
        XcursorImage* frame = frames->images[i] = XcursorImageCreate(32, 32);
        assert(frame);
        frames->nimage++;
        frame->xhot = frame->yhot = 16;
        frame->delay = 150; // Cursor animation protocol timing, not a readiness wait.
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++)
            frame->pixels[y * 32 + x] = (x == 16 || y == 16) ? 0xff000000 :
                    (x / 16 == i % 2 && y / 16 == i / 2) ? 0x80008040 : 0;
    }
    cursors[4] = XcursorImagesLoadCursor(display, frames);
    XcursorImagesDestroy(frames);
    cursors[5] = None;
    cursors[6] = XCreateFontCursor(display, XC_X_cursor);
    Window panes[PANE_COUNT];
    for (int i = 0; i < PANE_COUNT; i++) {
        panes[i] = XCreateSimpleWindow(display, window, i * 150, 0, 150, 350, 1, 0x606060, 0xf0f0f0);
        XDefineCursor(display, panes[i], cursors[i]);
        XSelectInput(display, panes[i], ExposureMask | EnterWindowMask);
        XMapWindow(display, panes[i]);
    }
    GC gc = XCreateGC(display, window, 0, NULL);
    XMapWindow(display, window);
    XFlush(display);
    for (;;) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type == KeyPress || (event.type == ClientMessage && (Atom)event.xclient.data.l[0] == close)) break;
        if (event.type == ConfigureNotify && event.xconfigure.window == window)
            for (int i = 0; i < PANE_COUNT; i++) XMoveResizeWindow(display, panes[i], i * event.xconfigure.width / PANE_COUNT, 0,
                    event.xconfigure.width / PANE_COUNT, event.xconfigure.height);
        for (int i = 0; i < PANE_COUNT; i++) if (event.xany.window == panes[i]) {
            if (event.type == Expose) XDrawString(display, panes[i], gc, 12, 40, labels[i], strlen(labels[i]));
            if (event.type == EnterNotify) { printf("cursor=%s\n", labels[i]); fflush(stdout); }
        }
    }
    XFreeGC(display, gc);
    for (int i = 0; i < PANE_COUNT; i++) if (cursors[i]) XFreeCursor(display, cursors[i]);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}
