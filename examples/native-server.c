#include <android/looper.h>
#include <stdio.h>
#include <unistd.h>
#include "embedded.h"

static void ready(void* unused, const char* display) {
    (void)unused;
    fprintf(stderr, "native X11 ready :%s\n", display);
    int socket = lorieServerConnect();
    if (socket < 0) _exit(2);
    close(socket);
    lorieServerStop();
}

static void windowSize(const LorieWindowConstraints* c, int* width, int* height) {
    *width = *width < c->minWidth ? c->minWidth : *width > c->maxWidth ? c->maxWidth : *width;
    *height = *height < c->minHeight ? c->minHeight : *height > c->maxHeight ? c->maxHeight : *height;
}

int main(int argc, char** argv) {
    ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    const LorieServerCallbacks callbacks = {.ready = ready, .windowSize = windowSize};
    if (!lorieServerStart(argc - 1, (const char* const*)(argv + 1), &callbacks, NULL)) return 1;
    for (;;) ALooper_pollOnce(-1, NULL, NULL, NULL);
}
