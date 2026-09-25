#pragma once
#include <windowstr.h>
#include "family_geometry.h"

void lorieWindowModelInit(ScreenPtr screen);
void lorieWindowModelReset(void);
void lorieWindowModelObserve(void);
void lorieWindowInspect(uint32_t serial, XID window, unsigned limit);
void lorieWindowModelRefresh(void);
void lorieWindowManagerReady(void);
Bool lorieWindowManagerExternal(void);
void lorieWindowFullscreenConfirm(XID window, uint32_t serial, Bool fullscreen);
void lorieWindowMaximizedConfirm(XID window, uint32_t serial, unsigned axes);
Bool lorieWindowGestureAllowed(XID window, unsigned button);
Bool lorieWindowBelongsTo(WindowPtr window, WindowPtr owner);
void lorieWindowFamily(WindowPtr owner, void (*visit)(WindowPtr, void*), void* data);
void lorieWindowFamilyGeometry(WindowPtr owner, Bool childrenOnly, LorieFamilyGeometry* geometry);
void lorieWindowFocus(WindowPtr window);
void lorieWindowBlur(WindowPtr window);
void lorieWindowClose(XID window, Bool force);
void lorieWindowConstrainSize(WindowPtr window, int* width, int* height);
WindowPtr lorieWindowInitialDialogParent(WindowPtr window);
