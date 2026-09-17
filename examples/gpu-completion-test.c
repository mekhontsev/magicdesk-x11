#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include "../lorie/src/main/cpp/lorie/gpu_completion.h"
#include "../lorie/src/main/cpp/lorie/socket_io.h"

static void* notify(void* arg) {
    int fd = *(int*)arg;
    for (int n = 0; n < 100000; ++n) assert(lorieNotifyGpuCompletion(fd));
    return NULL;
}
int main(void) {
    int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK), sockets[2];
    assert(fd >= 0 && !socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
    assert(!lorieConsumeGpuCompletion(fd));
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, notify, &fd));
    for (uint32_t i = 0; i < 10000; ++i) {
        uint32_t received = 0;
        assert(lorieWriteFully(sockets[0], &i, sizeof(i)));
        assert(lorieReadFully(sockets[1], &received, sizeof(received)) && received == i);
    }
    assert(!pthread_join(thread, NULL));
    assert(lorieConsumeGpuCompletion(fd) && !lorieConsumeGpuCompletion(fd));
    uint64_t saturated = UINT64_MAX - 1;
    assert(write(fd, &saturated, sizeof(saturated)) == sizeof(saturated));
    assert(lorieNotifyGpuCompletion(fd));
    assert(lorieConsumeGpuCompletion(fd));
    close(fd); close(sockets[0]); close(sockets[1]);
    puts("X11 nonblocking GPU notifications remain separate from command traffic");
}
