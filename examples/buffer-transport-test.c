#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/eventfd.h>
#include "../lorie/src/main/cpp/lorie/buffer.h"
#include "../lorie/src/main/cpp/lorie/dma_buffer.h"

static bool failDmaAllocation;
static int testDmaAllocate(size_t size) {
    if (failDmaAllocation) { errno = EACCES; return -1; }
    return lorieDmaAllocate(size);
}
#define lorieDmaAllocate testDmaAllocate

static int fakeHardware, released, lockError;
static AHardwareBuffer* fake = (AHardwareBuffer*)&fakeHardware;
static int testAllocate(const AHardwareBuffer_Desc* desc, AHardwareBuffer** out) {
    if (!fakeHardware) return AHardwareBuffer_allocate(desc, out);
    *out = fake;
    return 0;
}
static void testDescribe(const AHardwareBuffer* buffer, AHardwareBuffer_Desc* desc) {
    if (buffer != fake) { AHardwareBuffer_describe(buffer, desc); return; }
    *desc = (AHardwareBuffer_Desc){.width=4, .height=4, .stride=4, .layers=1,
        .format=AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM};
}
static void testRelease(AHardwareBuffer* buffer) {
    if (buffer == fake) ++released;
    else AHardwareBuffer_release(buffer);
}
static int testLock(AHardwareBuffer* buffer, uint64_t usage, int32_t fence, const ARect* rect, void** out) {
    if (buffer != fake) return AHardwareBuffer_lock(buffer, usage, fence, rect, out);
    *out = (void*)123;
    return lockError;
}
#define AHardwareBuffer_allocate testAllocate
#define AHardwareBuffer_describe testDescribe
#define AHardwareBuffer_release testRelease
#define AHardwareBuffer_lock testLock
#include "../lorie/src/main/cpp/lorie/buffer.c"

static int fdCount(void) {
    DIR* dir = opendir("/proc/self/fd");
    assert(dir);
    int count = 0;
    while (readdir(dir)) ++count;
    closedir(dir);
    return count;
}

static void roundTrip(off_t offset) {
    int fd = memfd_create("buffer-test", MFD_CLOEXEC), sockets[2];
    assert(fd >= 0 && ftruncate(fd, offset + 44) == 0);
    uint32_t value = 0xaabbccdd;
    assert(pwrite(fd, &value, 4, offset) == 4);
    LorieBuffer *source = LorieBuffer_wrapFileDescriptor(3, 8, 2,
        AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM, fd, offset), *received = NULL;
    assert(source && socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(!LorieBuffer_isDmaBuf(source) && !LorieBuffer_makeDmaBuf(source));
    source->locked = 1; source->lockedData = source->desc.data; source->graphicsImage = (void*)123;
    LorieBuffer_acquire(source);
    assert(LorieBuffer_sendHandleToUnixSocket(source, sockets[0]));
    LorieBuffer_recvHandleFromUnixSocket(sockets[1], &received);
    assert(received && *(uint32_t*)received->desc.data == value && received->offset == offset);
    assert(received->refcount == 1 && !received->locked && !received->lockedData &&
        !received->graphicsImage && !received->releaseGraphicsImage);
    assert(received->desc.id == source->desc.id && !received->desc.buffer);
    assert(fcntl(received->fd, F_GETFD) & FD_CLOEXEC);
    source->graphicsImage = NULL;
    LorieBuffer_release(received); LorieBuffer_release(source); LorieBuffer_release(source);
    close(fd); close(sockets[0]); close(sockets[1]);
}

struct Transfer { int socket, fd, length; LorieBufferWire wire; };
static void* fragments(void* arg) {
    struct Transfer* transfer = arg;
    for (int i = 0; i < transfer->length; ++i)
        assert(lorieWriteFully(transfer->socket, (char*)&transfer->wire + i, 1));
    if (transfer->length == sizeof(transfer->wire)) assert(ancil_send_fd(transfer->socket, transfer->fd) == 0);
    shutdown(transfer->socket, SHUT_WR);
    return NULL;
}
static void fragmentedTransfer(void) {
    int fd = memfd_create("fragment-test", MFD_CLOEXEC);
    assert(fd >= 0 && ftruncate(fd, 4) == 0);
    for (int length = 0; length <= sizeof(LorieBufferWire); ++length) {
        int sockets[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        struct Transfer transfer = {.socket=sockets[0], .fd=fd, .length=length,
            .wire={.id=87, .width=1, .height=1, .stride=1,
                .type=LORIEBUFFER_FD, .format=AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM}};
        pthread_t thread;
        assert(!pthread_create(&thread, NULL, fragments, &transfer));
        LorieBuffer* received = (void*)1;
        LorieBuffer_recvHandleFromUnixSocket(sockets[1], &received);
        assert((received != NULL) == (length == sizeof(LorieBufferWire)));
        if (received) assert(received->desc.id == 87);
        LorieBuffer_release(received);
        assert(!pthread_join(thread, NULL));
        close(sockets[0]); close(sockets[1]);
    }
    close(fd);
}

static void validation(void) {
    int fd = memfd_create("layout-test", MFD_CLOEXEC);
    assert(fd >= 0 && !ftruncate(fd, 4096));
    assert(!LorieBuffer_wrapFileDescriptor(1024, 1024, 1024, 5, fd, 0));
    assert(!LorieBuffer_wrapFileDescriptor(1024, 1, 32, 5, fd, 0));
    assert(!LorieBuffer_wrapFileDescriptor(1, 1, 1, 5, fd, -4));
    assert(!LorieBuffer_wrapFileDescriptor(1, 1, 1, 5, fd, 4096));
    assert(!LorieBuffer_wrapFileDescriptor(1, 1, 1, 5, fd, 2));
    assert(!LorieBuffer_wrapFileDescriptor(1, 1, 1, 77, fd, 0));
    assert(!LorieBuffer_wrapFileDescriptor(1, INT32_MAX, INT32_MAX, 5, fd, 0));
    assert(fcntl(fd, F_GETFD) >= 0);
    close(fd);
}

static void ancillary(void) {
    int sockets[2], fd = open("/dev/null", O_RDONLY);
    assert(fd >= 0 && !socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
    assert(lorieWriteFully(sockets[0], "!", 1));
    assert(ancil_recv_fd(sockets[1]) == -1);
    assert(ancil_send_fd(sockets[0], fd) == 0);
    int received = ancil_recv_fd(sockets[1]);
    assert(received >= 0 && (fcntl(received, F_GETFD) & FD_CLOEXEC));
    close(received);
    union { struct cmsghdr align; char data[CMSG_SPACE(3 * sizeof(int))]; } control = {0};
    char byte = '!';
    struct iovec iov = {&byte, 1};
    struct msghdr message = {.msg_iov=&iov, .msg_iovlen=1, .msg_control=&control, .msg_controllen=sizeof(control)};
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&message);
    cmsg->cmsg_level = SOL_SOCKET; cmsg->cmsg_type = SCM_RIGHTS; cmsg->cmsg_len = CMSG_LEN(3 * sizeof(int));
    int descriptors[] = {fd, fd, fd};
    memcpy(CMSG_DATA(cmsg), descriptors, sizeof(descriptors));
    assert(sendmsg(sockets[0], &message, MSG_NOSIGNAL) == 1);
    assert(ancil_recv_fd(sockets[1]) == -1);
    close(sockets[1]);
    assert(ancil_send_fd(sockets[0], fd) == -1);
    assert(!lorieWriteFully(sockets[0], "x", 1));
    close(fd); close(sockets[0]);
}

static void failedLock(void) {
    fakeHardware = 1; lockError = EBUSY;
    LorieBuffer* hardware = LorieBuffer_wrapAHardwareBuffer(fake);
    void* out = (void*)1;
    assert(hardware && LorieBuffer_lock(hardware, &out) == EBUSY);
    assert(!out && !hardware->locked && !hardware->lockedData);
    assert(LorieBuffer_lock(hardware, &out) == EBUSY);
    LorieBuffer_release(hardware); assert(released == 1);
    LorieBuffer* regular = LorieBuffer_allocate(4, 4, 5, LORIEBUFFER_REGULAR);
    assert(regular);
    void* original = regular->desc.data;
    LorieBuffer_convert(regular, LORIEBUFFER_AHARDWAREBUFFER, 5);
    assert(regular->desc.type == LORIEBUFFER_REGULAR && regular->desc.data == original && released == 2);
    LorieBuffer_release(regular);
}

static void dmaUnavailable(void) {
    LorieBuffer *buffer = LorieBuffer_allocate(19, 7, 5, LORIEBUFFER_REGULAR);
    assert(buffer);
    void *original = buffer->desc.data;
    memset(original, 0x35, 19 * 7 * 4);
    failDmaAllocation = true;
    assert(!LorieBuffer_makeDmaBuf(buffer));
    failDmaAllocation = false;
    assert(buffer->desc.type == LORIEBUFFER_REGULAR && buffer->desc.data == original &&
           buffer->desc.stride == 19 && !LorieBuffer_isDmaBuf(buffer));
    void *pixels;
    assert(!LorieBuffer_lock(buffer, &pixels) && ((uint32_t *)pixels)[19 * 7 - 1] == 0x35353535);
    assert(!LorieBuffer_unlock(buffer));
    LorieBuffer_release(buffer);
}

static void dmaStorage(void) {
    int probe = lorieDmaAllocate(4096);
    if (probe < 0) { puts("SKIP real DMA-heap storage: unavailable to this identity"); return; }
    close(probe);
    LorieBuffer *buffer = LorieBuffer_allocate(19, 7, 5, LORIEBUFFER_REGULAR);
    assert(buffer && !LorieBuffer_isDmaBuf(buffer));
    const uint64_t id = buffer->desc.id;
    void *pixels;
    assert(!LorieBuffer_lock(buffer, &pixels));
    memset(pixels, 0x35, 19 * 7 * 4);
    assert(!LorieBuffer_makeDmaBuf(buffer));
    assert(!LorieBuffer_unlock(buffer));
    LorieBuffer_acquire(buffer);
    assert(!LorieBuffer_makeDmaBuf(buffer));
    LorieBuffer_release(buffer);
    assert(LorieBuffer_makeDmaBuf(buffer) && LorieBuffer_isDmaBuf(buffer));
    assert(buffer->desc.id == id && buffer->desc.stride == 64);
    int fd = fcntl(buffer->fd, F_DUPFD_CLOEXEC, 0);
    LorieBuffer *reader = LorieBuffer_wrapFileDescriptor(19, 64, 7, 5, fd, 0);
    assert(reader && LorieBuffer_isDmaBuf(reader));
    assert(LorieBuffer_makeDmaBuf(buffer));
    assert(!LorieBuffer_lock(reader, &pixels) && *(uint32_t *)pixels == 0x35353535);
    assert(!LorieBuffer_unlock(reader));
    assert(!LorieBuffer_lock(buffer, &pixels));
    ((uint32_t *)pixels)[6 * 64 + 18] = 0x11223344;
    assert(!LorieBuffer_unlock(buffer));
    LorieBuffer_release(buffer);
    assert(!LorieBuffer_lock(reader, &pixels) && ((uint32_t *)pixels)[6 * 64 + 18] == 0x11223344);
    assert(!LorieBuffer_unlock(reader));
    LorieBuffer_release(reader);
    close(fd);
    puts("DMA-BUF promotion preserves pixels, identity, live updates and independent FD lifetime");
}

int main(void) {
    int before = fdCount();
    validation(); roundTrip(0); roundTrip(4); roundTrip(sysconf(_SC_PAGESIZE));
    fragmentedTransfer(); ancillary(); dmaUnavailable(); dmaStorage(); failedLock();
    assert(fdCount() == before);
    puts("X11 FD layouts, offsets, wire fragments/EOF, local ownership, FD cleanup and failed AHB locks passed");
}
