#include "dma_copy.h"
#include <android/sharedmem.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static bool hideVulkan = true;
void* __real_dlopen(const char*, int);
void* __wrap_dlopen(const char* name, int flags) {
    return hideVulkan && !strcmp(name, "libvulkan.so") ? NULL : __real_dlopen(name, flags);
}

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    if (dmaCopyCreate()) {
        puts("FAIL Vulkan is mandatory");
        return 20;
    }
    hideVulkan = false;
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets))
        return 3;
    pid_t child = fork();
    if (!child) {
        close(sockets[0]);
        char value[24];
        snprintf(value, sizeof(value), "%d", sockets[1]);
        execl(argv[1], argv[1], value, NULL);
        _exit(127);
    }
    close(sockets[1]);
    char byte, control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec io = {&byte, 1};
    struct msghdr message = {
        .msg_iov = &io, .msg_iovlen = 1, .msg_control = control, .msg_controllen = sizeof(control)};
    if (recvmsg(sockets[0], &message, 0) != 1)
        return 4;
    struct cmsghdr* header = CMSG_FIRSTHDR(&message);
    if (!header || header->cmsg_type != SCM_RIGHTS)
        return 5;
    int fd = *(int*)CMSG_DATA(header);
    struct stat st;
    fstat(fd, &st);
    void* pixels = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED)
        return 6;
    DmaCopyContext* context = dmaCopyCreate();
    if (!context) {
        puts("FAIL no context");
        return 7;
    }
    int ordinary = ASharedMemory_create("copy-fallback-test", 1048576);
    if (ordinary < 0)
        return 21;
    if (dmaCopyImportSource(context, ordinary, 0, 300, 300, 1280, pixels)) {
        puts("FAIL accepted non-DMA FD");
        return 22;
    }
    close(ordinary);
    DmaCopySource* source = dmaCopyImportSource(context, fd, 0, 300, 300, 1280, pixels);
    if (!source) {
        puts("FAIL no source");
        return 8;
    }
    DmaCopySource* offsetSource = dmaCopyImportSource(context, fd, 128, 280, 299, 1280, (char*)pixels + 128);
    if (!offsetSource) {
        puts("FAIL offset source");
        return 8;
    }
    const unsigned width = 312, height = 317;
    AHardwareBuffer_Desc desc = {
        .width = width,
        .height = height,
        .layers = 1,
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM,
        .usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER |
                 AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN};
    AHardwareBuffer* buffer;
    if (AHardwareBuffer_allocate(&desc, &buffer))
        return 9;
    AHardwareBuffer_describe(buffer, &desc);
    uint32_t *actual, *expected = calloc(width * height, sizeof(uint32_t));
    if (AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, NULL, (void**)&actual))
        return 10;
    for (unsigned y = 0; y < height; y++)
        for (unsigned x = 0; x < width; x++)
            actual[y * desc.stride + x] = expected[y * width + x] = 0xff332211;
    AHardwareBuffer_unlock(buffer, NULL);
    DmaCopyDestination* destination = dmaCopyImportDestination(context, buffer);
    if (!destination) {
        puts("FAIL no destination");
        return 11;
    }
    const int cases[][7] = {{0, 0, 0, 0, 300, 300, 0},
                            {5, 7, 7, 9, 290, 287, 0},
                            {4, 294, 5, 309, 290, 6, 0},
                            {0, 0, 10, 5, 280, 299, 1}};
    unsigned mismatches = 0;
    for (unsigned i = 0; i < 4; i++) {
        const int* r = cases[i];
        if (dmaCopyRect(r[6] ? offsetSource : source, destination, r[0], r[1], r[2], r[3], r[4], r[5]) != DMA_COPY_COMPLETE)
            return 12;
        for (unsigned y = 0; y < (unsigned)r[5]; y++)
            for (unsigned x = 0; x < (unsigned)r[4]; x++)
                expected[(y + r[3]) * width + x + r[2]] =
                    0xff000000u | ((((r[6] ? 32 : 0) + (y + r[1]) * 320 + x + r[0]) * 167u) & 0xffffff);
        if (AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, NULL, (void**)&actual))
            return 13;
        for (unsigned y = 0; y < height; y++)
            for (unsigned x = 0; x < width; x++)
                if (actual[y * desc.stride + x] != expected[y * width + x]) {
                    if (mismatches < 5)
                        printf("bad case=%u x=%u y=%u actual=%08x expected=%08x\n", i, x, y,
                               actual[y * desc.stride + x], expected[y * width + x]);
                    mismatches++;
                }
        AHardwareBuffer_unlock(buffer, NULL);
        printf("case=%u mismatches=%u\n", i, mismatches);
    }
    if (dmaCopyRect(source, destination, -1, 0, 0, 0, 1, 1) != DMA_COPY_DECLINED)
        return 14;
    printf("staged=%llu\n", (unsigned long long)dmaCopyTakeStagedBytes(context));
    dmaCopyRelease(context);
    dmaCopyReleaseSource(source);
    dmaCopyReleaseSource(offsetSource);
    dmaCopyReleaseDestination(destination);
    AHardwareBuffer_release(buffer);
    free(expected);
    munmap(pixels, st.st_size);
    close(fd);
    close(sockets[0]);
    waitpid(child, NULL, 0);
    return mismatches ? 1 : 0;
}
