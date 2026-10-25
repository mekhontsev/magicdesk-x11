#include <dix-config.h>
#include "scrnintstr.h"
#include "randrstr.h"
#include "extension.h"
#include "extinit.h"
#include "vidmodestr.h"
#include "vidmode.h"

static DevPrivateKeyRec modeKey;

/* Legacy GLX timing queries must describe the same virtual CRTC as RandR.
 * The Android output owner, not a client VidMode request, controls its mode. */
static Bool currentMode(ScreenPtr screen, DisplayModePtr *result, int *clock) {
    RRCrtcPtr crtc = RRFirstEnabledCrtc(screen);
    if (!crtc || !crtc->mode) return FALSE;
    const xRRModeInfo *info = &crtc->mode->mode;
    DisplayModePtr mode = dixLookupPrivate(&screen->devPrivates, &modeKey);
    *mode = (DisplayModeRec) {
        .name = "screen", .status = MODE_OK, .Clock = info->dotClock / 1000,
        .HDisplay = info->width, .HSyncStart = info->hSyncStart,
        .HSyncEnd = info->hSyncEnd, .HTotal = info->hTotal, .HSkew = info->hSkew,
        .VDisplay = info->height, .VSyncStart = info->vSyncStart,
        .VSyncEnd = info->vSyncEnd, .VTotal = info->vTotal, .VScan = 1,
        .Flags = info->modeFlags,
    };
    *result = mode;
    *clock = mode->Clock;
    return TRUE;
}

static Bool nextMode(ScreenPtr screen, DisplayModePtr *mode, int *clock) { return FALSE; }
static int countModes(ScreenPtr screen) {
    RRCrtcPtr crtc = RRFirstEnabledCrtc(screen);
    return crtc && crtc->mode ? 1 : 0;
}
static vidMonitorValue monitorValue(ScreenPtr screen, int type, int index) {
    vidMonitorValue value = { .i = 0 };
    DisplayModePtr mode;
    int clock;
    if (type == VIDMODE_MON_VENDOR || type == VIDMODE_MON_MODEL) {
        value.ptr = type == VIDMODE_MON_VENDOR ? "MagicDesk" : "screen";
    } else if (!index && currentMode(screen, &mode, &clock)) {
        switch (type) {
        case VIDMODE_MON_NHSYNC: case VIDMODE_MON_NVREFRESH: value.i = 1; break;
        case VIDMODE_MON_HSYNC_LO: case VIDMODE_MON_HSYNC_HI:
            value.f = mode->HTotal ? (float) clock / mode->HTotal : 0; break;
        case VIDMODE_MON_VREFRESH_LO: case VIDMODE_MON_VREFRESH_HI:
            value.f = mode->HTotal && mode->VTotal
                    ? (float) clock * 1000 / mode->HTotal / mode->VTotal : 0; break;
        }
    }
    return value;
}
static Bool getViewport(ScreenPtr screen, int *x, int *y) {
    RRCrtcPtr crtc = RRFirstEnabledCrtc(screen);
    if (!crtc) return FALSE;
    *x = crtc->x; *y = crtc->y;
    return TRUE;
}
static int countClocks(ScreenPtr screen, Bool *programmable) {
    *programmable = FALSE;
    return countModes(screen);
}
static Bool getClocks(ScreenPtr screen, int *clock) {
    DisplayModePtr mode;
    return currentMode(screen, &mode, clock);
}
static int dotClock(ScreenPtr screen, int clock) { return clock; }

/* Reject unsupported mutations through the upstream protocol's error paths. */
static Bool changeMode(ScreenPtr screen, DisplayModePtr mode) { return FALSE; }
static Bool zoom(ScreenPtr screen, int amount) { return FALSE; }
static Bool lockZoom(ScreenPtr screen, Bool lock) { return FALSE; }
static Bool setViewport(ScreenPtr screen, int x, int y) { return FALSE; }
static ModeStatus checkMode(ScreenPtr screen, DisplayModePtr mode) { return MODE_BAD; }
static void setCrtc(ScreenPtr screen, DisplayModePtr mode) { }
static Bool setGamma(ScreenPtr screen, float red, float green, float blue) { return FALSE; }
static Bool getGamma(ScreenPtr screen, float *red, float *green, float *blue) { return FALSE; }
static Bool gammaRamp(ScreenPtr screen, int size, CARD16 *red, CARD16 *green, CARD16 *blue) { return FALSE; }
static int gammaSize(ScreenPtr screen) { return 0; }

static void initExtension(void) {
    if (!dixRegisterPrivateKey(&modeKey, PRIVATE_SCREEN, sizeof(DisplayModeRec))) return;
    int initialized = 0;
    for (int i = 0; i < screenInfo.numScreens; ++i) {
        ScreenPtr screen = screenInfo.screens[i];
        if (!countModes(screen)) continue;
        VidModePtr vm = VidModeInit(screen);
        if (!vm) continue;
        vm->GetMonitorValue = monitorValue;
        vm->GetCurrentModeline = vm->GetFirstModeline = currentMode;
        vm->GetNextModeline = nextMode;
        vm->GetNumOfModes = countModes;
        vm->GetViewPort = getViewport;
        vm->GetNumOfClocks = countClocks;
        vm->GetClocks = getClocks;
        vm->GetDotClock = dotClock;
        vm->DeleteModeline = vm->SwitchMode = vm->AddModeline = changeMode;
        vm->ZoomViewport = zoom;
        vm->LockZoom = lockZoom;
        vm->SetViewPort = setViewport;
        vm->CheckModeForMonitor = vm->CheckModeForDriver = checkMode;
        vm->SetCrtcForMode = setCrtc;
        vm->SetGamma = setGamma;
        vm->GetGamma = getGamma;
        vm->SetGammaRamp = vm->GetGammaRamp = gammaRamp;
        vm->GetGammaRampSize = gammaSize;
        ++initialized;
    }
    if (initialized) VidModeAddExtension(FALSE);
}

void lorieVidModeRegister(void) {
    static Bool registered;
    if (registered) return;
    const ExtensionModule module = { initExtension, "XFree86-VidModeExtension", NULL };
    LoadExtensionList(&module, 1, FALSE);
    registered = TRUE;
}
