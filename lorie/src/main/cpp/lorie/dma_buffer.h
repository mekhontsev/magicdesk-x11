#pragma once

#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

static inline int lorieDmaSync(int fd, uint64_t flags) {
    struct dma_buf_sync sync = {.flags = flags};
    int result;
    do { result = ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync); } while (result < 0 && errno == EINTR);
    return result < 0 ? errno : 0;
}

static inline int lorieDmaAllocate(size_t size) {
    if (!size) { errno = EINVAL; return -1; }
    int heap = open("/dev/dma_heap/system", O_RDONLY | O_CLOEXEC);
    if (heap < 0) return -1;
    struct dma_heap_allocation_data request = {.len = size, .fd_flags = O_RDWR | O_CLOEXEC};
    int result;
    do { result = ioctl(heap, DMA_HEAP_IOCTL_ALLOC, &request); } while (result < 0 && errno == EINTR);
    int error = errno;
    close(heap);
    if (result < 0) { errno = error; return -1; }
    return (int)request.fd;
}
