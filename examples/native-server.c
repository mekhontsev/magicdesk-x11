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

int main(int argc, char** argv) {
    ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    if (!lorieServerStart(argc - 1, (const char* const*)(argv + 1), ready, NULL)) return 1;
    for (;;) ALooper_pollOnce(-1, NULL, NULL, NULL);
}
