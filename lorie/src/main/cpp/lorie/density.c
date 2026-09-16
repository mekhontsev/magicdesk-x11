#include <dix-config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <windowstr.h>
#include <selection.h>
#include <property.h>
#include <propertyst.h>
#include <xacestr.h>
#include <randrstr.h>
#include "lorie.h"
#include "density_settings.h"

extern ScreenPtr pScreenPtr;
static XID settingsWindow;
static Atom settingsSelection;
static uint32_t serial;
static char resources[40];

static Atom atom(const char* name) { return MakeAtom(name, strlen(name), TRUE); }

static void selectionChanged(__unused CallbackListPtr* list, __unused void* data, void* args) {
    SelectionInfoRec* info = args;
    if (!settingsWindow || info->selection->selection != settingsSelection) return;
    if (info->kind == SelectionSetOwner && info->selection->window != settingsWindow) {
        XID previous = settingsWindow;
        settingsWindow = None;
        FreeResource(previous, RT_NONE);
    } else if (info->kind == SelectionWindowDestroy) settingsWindow = None;
}

static void publish(void) {
    WindowPtr root = pScreenPtr->root, owner = NULL;
    PropertyPtr previous = NULL;
    int found = dixLookupProperty(&previous, root, XA_RESOURCE_MANAGER, serverClient, DixReadAccess);
    // A Linux settings manager may replace the database. Never overwrite its resources.
    if (found == BadMatch || (found == Success && previous->type == XA_STRING && previous->format == 8
            && previous->size == strlen(resources) && !memcmp(previous->data, resources, previous->size))) {
        snprintf(resources, sizeof(resources), "Xft.dpi: %d\n", monitorResolution);
        dixChangeWindowProperty(serverClient, root, XA_RESOURCE_MANAGER, XA_STRING, 8,
                PropModeReplace, strlen(resources), resources, TRUE);
    }
    if (settingsWindow && dixLookupWindow(&owner, settingsWindow, serverClient, DixWriteAccess) == Success) {
        uint8_t bytes[LORIE_DENSITY_SETTINGS_SIZE];
        size_t length = lorieDensitySettings(bytes, monitorResolution, ++serial);
        Atom settings = atom("_XSETTINGS_SETTINGS");
        dixChangeWindowProperty(serverClient, owner, settings, settings, 8, PropModeReplace, length, bytes, TRUE);
    }
}

void lorieDensityInit(void) {
    settingsWindow = None;
    serial = 0;
    resources[0] = 0;
    // Whole Linux desktops own their toolkit settings daemon. Dedicated app sessions use ours.
    const char* enabled = getenv("MAGICDESK_X11_XSETTINGS");
    if (enabled && !strcmp(enabled, "1")) {
        settingsSelection = atom("_XSETTINGS_S0");
        Selection* selection = NULL;
        int found = dixLookupSelection(&selection, settingsSelection, serverClient, DixSetAttrAccess);
        if (found == BadMatch) {
            selection = dixAllocateObjectWithPrivates(Selection, PRIVATE_SELECTION);
            if (selection) {
                selection->selection = settingsSelection;
                if (XaceHookSelectionAccess(serverClient, &selection, DixCreateAccess | DixSetAttrAccess) != Success) {
                    free(selection);
                    selection = NULL;
                } else {
                    selection->next = CurrentSelections;
                    CurrentSelections = selection;
                }
            }
        } else if (found != Success || selection->window != None) selection = NULL;
        if (selection) {
            int error;
            WindowPtr owner = CreateWindow(FakeClientID(0), pScreenPtr->root, 0, 0, 1, 1, 0,
                    InputOnly, 0, NULL, 0, serverClient, CopyFromParent, &error);
            if (owner && AddResource(owner->drawable.id, RT_WINDOW, owner)) {
                settingsWindow = owner->drawable.id;
                selection->lastTimeChanged = currentTime;
                selection->window = settingsWindow;
                selection->pWin = owner;
                selection->client = serverClient;
                if (!AddCallback(&SelectionCallback, selectionChanged, NULL))
                    FatalError("Cannot observe X11 settings ownership\n");
                SelectionInfoRec info = {selection, serverClient, SelectionSetOwner};
                CallCallbacks(&SelectionCallback, &info);
            }
        }
    }
    publish();
    RROutputPtr output = RRFirstOutput(pScreenPtr);
    if (output) RROutputSetPhysicalSize(output, pScreenPtr->mmWidth, pScreenPtr->mmHeight);
    if (settingsWindow) {
        xEvent event = {0};
        event.u.u.type = ClientMessage;
        event.u.u.detail = 32;
        event.u.clientMessage.window = pScreenPtr->root->drawable.id;
        event.u.clientMessage.u.l.type = atom("MANAGER");
        event.u.clientMessage.u.l.longs0 = currentTime.milliseconds;
        event.u.clientMessage.u.l.longs1 = settingsSelection;
        event.u.clientMessage.u.l.longs2 = settingsWindow;
        DeliverEvents(pScreenPtr->root, &event, 1, NULL);
    }
}

void lorieDensityReset(void) {
    DeleteCallback(&SelectionCallback, selectionChanged, NULL);
    settingsWindow = None;
}

void lorieSetDpi(int dpi) {
    if (!pScreenPtr || !pScreenPtr->root || dpi < 24 || dpi > 1536 || dpi == monitorResolution) return;
    monitorResolution = dpi;
    pScreenPtr->mmWidth = max(1, pScreenPtr->width * 25.4 / dpi);
    pScreenPtr->mmHeight = max(1, pScreenPtr->height * 25.4 / dpi);
    RROutputPtr output = RRFirstOutput(pScreenPtr);
    if (output) RROutputSetPhysicalSize(output, pScreenPtr->mmWidth, pScreenPtr->mmHeight);
    RRScreenSizeNotify(pScreenPtr);
    publish();
}
