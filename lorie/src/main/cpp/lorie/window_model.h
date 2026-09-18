#pragma once
#include <windowstr.h>

void lorieWindowModelInit(ScreenPtr screen);
void lorieWindowModelReset(void);
void lorieWindowModelObserve(void);
void lorieWindowModelRefresh(void);
void lorieWindowManagerReady(void);
void lorieWindowFullscreenConfirm(XID window, uint32_t serial, Bool fullscreen);
Bool lorieWindowBelongsTo(WindowPtr window, WindowPtr owner);
void lorieWindowFamily(WindowPtr owner, void (*visit)(WindowPtr, void*), void* data);
void lorieWindowFocus(WindowPtr window);
void lorieWindowClose(XID window);
void lorieWindowConstrainSize(WindowPtr window, int* width, int* height);
