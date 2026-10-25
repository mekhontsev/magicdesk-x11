#include <X11/Xlib.h>
#include <X11/extensions/xf86vmode.h>
#include <X11/extensions/Xrandr.h>
#include <assert.h>
#include <stdio.h>

static int errors;
static int rejected(Display *display, XErrorEvent *error) { ++errors; return 0; }

int main(void) {
    Display *display = XOpenDisplay(NULL);
    assert(display);
    int screen = DefaultScreen(display), major, minor, clock;
    assert(XF86VidModeQueryVersion(display, &major, &minor));
    XF86VidModeModeLine line = {0};
    assert(XF86VidModeGetModeLine(display, screen, &clock, &line));
    XRRScreenResources *resources = XRRGetScreenResourcesCurrent(display, RootWindow(display, screen));
    assert(resources);
    int found = 0;
    for (int i = 0; i < resources->ncrtc; ++i) {
        XRRCrtcInfo *crtc = XRRGetCrtcInfo(display, resources, resources->crtcs[i]);
        assert(crtc);
        for (int j = 0; j < resources->nmode && crtc->mode; ++j) {
            XRRModeInfo *mode = &resources->modes[j];
            if (mode->id != crtc->mode) continue;
            assert(line.hdisplay == mode->width && line.vdisplay == mode->height);
            assert(line.htotal == mode->hTotal && line.vtotal == mode->vTotal);
            assert(line.hsyncstart == mode->hSyncStart && line.hsyncend == mode->hSyncEnd);
            assert(line.vsyncstart == mode->vSyncStart && line.vsyncend == mode->vSyncEnd);
            assert(clock == (int)(mode->dotClock / 1000));
            found = 1;
            break;
        }
        XRRFreeCrtcInfo(crtc);
        if (found) break;
    }
    assert(found);
    int count;
    XF86VidModeModeInfo **modes;
    assert(XF86VidModeGetAllModeLines(display, screen, &count, &modes));
    assert(count == 1 && modes[0]->dotclock == (unsigned)clock);
    XFree(modes);
    XSetErrorHandler(rejected);
    XF86VidModeSetViewPort(display, screen, 10, 10);
    assert(!XF86VidModeGetModeLine(display, ScreenCount(display), &clock, &line));
    XSync(display, False);
    assert(errors == 2);
    XRRFreeScreenResources(resources);
    XCloseDisplay(display);
    puts("VIDMODE_PASS");
    return 0;
}
