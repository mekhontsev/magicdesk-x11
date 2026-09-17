#pragma clang diagnostic ignored "-Wunknown-pragmas"
#pragma ide diagnostic ignored "bugprone-reserved-identifier"
#pragma ide diagnostic ignored "ConstantParameter"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
#pragma ide diagnostic ignored "OCUnusedMacroInspection"
#pragma ide diagnostic ignored "readability-redundant-declaration"
#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#ifndef __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__
#define __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__
#endif
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pixman.h>
#include <stdbool.h>
#include <linux/memfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/sharedmem.h>
#include "list.h"
#include "buffer.h"
#include "buffer_layout.h"
#include "socket_io.h"

// libEGL exports this only since API 26, weak so the library still loads below that.
__attribute__((weak)) EGLClientBuffer eglGetNativeClientBufferANDROID(const struct AHardwareBuffer* buffer);

struct LorieBuffer {
    int refcount;
    LorieBuffer_Desc desc;

    int8_t locked;
    void* lockedData;

    // file descriptor of shared memory fragment for shared memory backed buffer
    int fd;
    size_t size;
    off_t offset;
    void* mapping;

    GLuint id;
    EGLImage image;
    struct xorg_list link;

    int32_t gpuCopyPending;
};

void LorieBuffer_gpuCopyPendingInc(LorieBuffer* buffer) {
    if (buffer)
        buffer->gpuCopyPending++;
}

int LorieBuffer_fileDescriptor(LorieBuffer* buffer, off_t* offset) {
    if (!buffer || buffer->desc.type != LORIEBUFFER_FD) return -1;
    *offset = buffer->offset;
    return buffer->fd;
}

void LorieBuffer_gpuCopyPendingDec(LorieBuffer* buffer) {
    if (buffer)
        buffer->gpuCopyPending--;
}

bool LorieBuffer_hasGpuCopyPending(LorieBuffer* buffer) {
    return buffer && buffer->gpuCopyPending;
}

__attribute__((unused))
static int memfd_create(const char *name, unsigned int flags) {
#ifndef __NR_memfd_create
#if defined __i386__
#define __NR_memfd_create 356
#elif defined __x86_64__
    #define __NR_memfd_create 319
#elif defined __arm__
#define __NR_memfd_create 385
#elif defined __aarch64__
#define __NR_memfd_create 279
#endif
#endif

#ifdef __NR_memfd_create
    return syscall(__NR_memfd_create, name, flags); // NOLINT(cppcoreguidelines-narrowing-conversions)
#else
    errno = ENOSYS;
	return -1;
#endif
}

static inline size_t alignToPage(size_t size) {
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    return (size + page_size - 1) & ~(page_size - 1);
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnreachableCallsOfFunction"
int LorieBuffer_createRegion(char const* name, size_t size) {
    int fd = -1;
    if (__builtin_available(android 26, *))
        fd = ASharedMemory_create(name, size);
    if (fd >= 0)
        return fd;

    fd = memfd_create(name, MFD_CLOEXEC|MFD_ALLOW_SEALING);
    if (fd >= 0) {
        if (ftruncate(fd, (off_t)size) == 0) return fd;
        close(fd);
    }

    fd = open("/dev/ashmem", O_RDWR);
    if (fd < 0)
        return fd;

    char name_buffer[ASHMEM_NAME_LEN] = {0};
    strncpy(name_buffer, name, sizeof(name_buffer));
    name_buffer[sizeof(name_buffer)-1] = 0;

    int ret = ioctl(fd, ASHMEM_SET_NAME, name_buffer);
    if (ret < 0) goto error;

    ret = ioctl(fd, ASHMEM_SET_SIZE, size);
    if (ret < 0) goto error;

    return fd;
    error:
    close(fd);
    return ret;
}
#pragma clang diagnostic pop

static LorieBuffer* allocate(int32_t width, int32_t stride, int32_t height, int8_t format, int8_t type, AHardwareBuffer *buf, int fd, off_t offset, bool takeFd) {
    AHardwareBuffer_Desc desc = {0};
    static uint64_t id = 0;
    LorieBuffer b = { .refcount = 1,
        .desc = { .width = width, .stride = stride, .height = height, .format = format,
                  .type = type, .buffer = buf, .id = __sync_fetch_and_add(&id, 1) },
        .fd = takeFd ? fd : -1, .offset = offset };
    size_t bytes;
    if (type == LORIEBUFFER_AHARDWAREBUFFER) {
        if (!buf) goto fail;
        if (__builtin_available(android 26, *)) AHardwareBuffer_describe(buf, &desc);
        if (desc.layers != 1 || desc.width > INT32_MAX || desc.height > INT32_MAX ||
            desc.stride > INT32_MAX || desc.format > UINT8_MAX) goto fail;
        b.desc.width = (int32_t)desc.width;
        b.desc.height = (int32_t)desc.height;
        b.desc.stride = (int32_t)desc.stride;
        b.desc.format = desc.format;
    }
    if ((b.desc.format != AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM &&
         b.desc.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM &&
         b.desc.format != AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM) ||
        !lorieBufferLayout(b.desc.width, b.desc.stride, b.desc.height, offset, &bytes)) goto fail;

    switch (type) {
        case LORIEBUFFER_REGULAR:
            b.desc.data = calloc(1, bytes);
            if (!b.desc.data)
                goto fail;
            break;
        case LORIEBUFFER_FD: {
            if (!takeFd) b.fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
            struct stat st;
            if (b.fd < 0 || fstat(b.fd, &st)) goto fail;
            uint64_t extent = st.st_size > 0 ? (uint64_t)st.st_size : 0;
            if (!extent) {
                int ashmemSize = ioctl(b.fd, ASHMEM_GET_SIZE, NULL);
                if (ashmemSize > 0) extent = (uint64_t)ashmemSize;
            }
            if ((uint64_t)offset + bytes > extent) goto fail;
            long page = sysconf(_SC_PAGE_SIZE);
            if (page <= 0) goto fail;
            size_t delta = (size_t)(offset % page);
            if (bytes > SIZE_MAX - delta) goto fail;
            b.size = bytes + delta;
            b.mapping = mmap(NULL, b.size, PROT_READ|PROT_WRITE, MAP_SHARED, b.fd, offset - delta);
            if (b.mapping == MAP_FAILED) { b.mapping = NULL; goto fail; }
            b.desc.data = (char*)b.mapping + delta;
            break;
        }
        case LORIEBUFFER_AHARDWAREBUFFER:
            break;
        default: goto fail;
    }

    LorieBuffer* buffer = calloc(1, sizeof(*buffer));
    if (!buffer) goto fail;

    *buffer = b;
    xorg_list_init(&buffer->link);
    return buffer;
fail:
    if (b.mapping) munmap(b.mapping, b.size);
    if (b.fd >= 0) close(b.fd);
    if (type == LORIEBUFFER_REGULAR) free(b.desc.data);
    if (buf) {
        if (__builtin_available(android 26, *)) AHardwareBuffer_release(buf);
    }
    return NULL;
}

__LIBC_HIDDEN__ LorieBuffer* LorieBuffer_allocate(int32_t width, int32_t height, int8_t format, int8_t type) {
    int fd = -1;
    size_t size = 0;
    AHardwareBuffer *ahardwarebuffer = NULL;
    if (!lorieBufferLayout(width, width, height, 0, &size) ||
        size > SIZE_MAX - (size_t)sysconf(_SC_PAGE_SIZE)) return NULL;

    if (type == LORIEBUFFER_FD) {
        size = alignToPage(size);
        fd = LorieBuffer_createRegion("LorieBuffer", size);
        if (fd < 0)
            return NULL;
    } else if (type == LORIEBUFFER_AHARDWAREBUFFER) {
        AHardwareBuffer_Desc desc = { .width = width, .height = height, .format = format, .layers = 1,
                .usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER };
        int err = -1;
        if (__builtin_available(android 26, *))
            err = AHardwareBuffer_allocate(&desc, &ahardwarebuffer);
        if (err != 0)
            dprintf(2, "FATAL: failed to allocate AHardwareBuffer (width %d height %d format %d): error %d\n", width, height, format, err);
    }

    return allocate(width, width, height, format, type, ahardwarebuffer, fd, 0, true);
}

__LIBC_HIDDEN__ LorieBuffer* LorieBuffer_wrapFileDescriptor(int32_t width, int32_t stride, int32_t height, int8_t format, int fd, off_t offset) {
    return allocate(width, stride, height, format, LORIEBUFFER_FD, NULL, fd, offset, false);
}

__LIBC_HIDDEN__ LorieBuffer* LorieBuffer_wrapAHardwareBuffer(AHardwareBuffer* buffer) {
    return allocate(0, 0, 0, 0, LORIEBUFFER_AHARDWAREBUFFER, buffer, -1, 0, false);
}

__LIBC_HIDDEN__ void LorieBuffer_convert(LorieBuffer* buffer, int8_t type, int8_t format) {
    void *data;
    if (!buffer || buffer->desc.type != LORIEBUFFER_REGULAR
        || (type != LORIEBUFFER_FD && type != LORIEBUFFER_AHARDWAREBUFFER)
        || (format != AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM && format != AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM))
        return;

    if (type == LORIEBUFFER_FD) {
        size_t size;
        if (!lorieBufferLayout(buffer->desc.width, buffer->desc.stride, buffer->desc.height, 0, &size)) return;
        int fd = LorieBuffer_createRegion("LorieBuffer", size);
        if (fd < 0)
            return;

        data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        if (!data || data == MAP_FAILED) {
            close(fd);
            return;
        }

        pixman_blt(buffer->desc.data, data, buffer->desc.stride, buffer->desc.stride, 32, 32, 0, 0, 0, 0, buffer->desc.width, buffer->desc.height);

        buffer->desc.type = type;
        buffer->desc.format = format;
        buffer->fd = fd;
        buffer->size = size;
        buffer->offset = 0;
        buffer->mapping = data;
        free(buffer->desc.data);
        buffer->desc.data = data;

        buffer->lockedData = NULL;
        buffer->locked = 0;
    } else {
        AHardwareBuffer *b = NULL;
        AHardwareBuffer_Desc desc = { .width = buffer->desc.width, .height = buffer->desc.height, .format = format, .layers = 1,
                .usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN | AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER };
        int err = -1;
        if (__builtin_available(android 26, *))
            err = AHardwareBuffer_allocate(&desc, &b);
        if (err != 0)
            return;

        if (__builtin_available(android 26, *))
            AHardwareBuffer_describe(b, &desc);

        if (__builtin_available(android 26, *)) {
            if (AHardwareBuffer_lock(b, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, NULL, &data) != 0) {
                AHardwareBuffer_release(b);
                return;
            }
            pixman_blt(buffer->desc.data, data, buffer->desc.stride, (int) desc.stride, 32, 32, 0, 0, 0, 0, buffer->desc.width, buffer->desc.height);
            AHardwareBuffer_unlock(b, NULL);
        }

        buffer->desc.type = type;
        buffer->desc.format = format;
        buffer->desc.stride = (int32_t) desc.stride;
        buffer->desc.buffer = b;
        free(buffer->desc.data);
        buffer->desc.data = NULL;
        buffer->lockedData = NULL;
        buffer->locked = 0;
    }
}

__LIBC_HIDDEN__ void __LorieBuffer_free(LorieBuffer* buffer) {
    if (!buffer)
        return;

    xorg_list_del(&buffer->link);

    if (eglGetCurrentContext())
        glDeleteTextures(1, &buffer->id);

    if (eglGetCurrentDisplay() && buffer->image)
        eglDestroyImageKHR(eglGetCurrentDisplay(), buffer->image);

    switch (buffer->desc.type) {
        case LORIEBUFFER_REGULAR:
            free(buffer->desc.data);
            break;
        case LORIEBUFFER_FD:
            munmap(buffer->mapping, buffer->size);
            close(buffer->fd);
            break;
        case LORIEBUFFER_AHARDWAREBUFFER:
            if (__builtin_available(android 26, *))
                AHardwareBuffer_release(buffer->desc.buffer);
            break;
        default: break;
    }

    free(buffer);
}

__LIBC_HIDDEN__ const LorieBuffer_Desc* LorieBuffer_description(LorieBuffer* buffer) {
    static const LorieBuffer_Desc none = {0};
    return buffer ? &buffer->desc : &none;
}

__LIBC_HIDDEN__ int LorieBuffer_lock(LorieBuffer* buffer, void** out) {
    int ret = 0;
    if (out) *out = NULL;
    if (!buffer)
        return ENODEV;

    if (buffer->locked) {
        dprintf(2, "tried to lock already locked buffer\n");
        if (out)
            *out = buffer->lockedData;
        return EEXIST;
    }

    if (buffer->desc.type == LORIEBUFFER_REGULAR || buffer->desc.type == LORIEBUFFER_FD)
        buffer->lockedData = buffer->desc.data;
    else if (buffer->desc.type == LORIEBUFFER_AHARDWAREBUFFER) {
        ret = ENOSYS;
        if (__builtin_available(android 26, *))
            ret = AHardwareBuffer_lock(buffer->desc.buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, NULL, &buffer->lockedData);
    }
    if (ret) {
        buffer->lockedData = NULL;
        return ret;
    }

    if (out)
        *out = buffer->lockedData;

    buffer->locked = 1;

    return ret;
}

__LIBC_HIDDEN__ int LorieBuffer_unlock(LorieBuffer* buffer) {
    int ret = 0;
    if (!buffer)
        return ENODEV;

    if (!buffer->locked) {
        dprintf(2, "tried to unlock non-locked buffer\n");
        return ENOENT;
    }

    if (buffer->desc.type == LORIEBUFFER_AHARDWAREBUFFER) {
        if (__builtin_available(android 26, *))
            ret = AHardwareBuffer_unlock(buffer->desc.buffer, NULL);
    }

    buffer->lockedData = NULL;
    buffer->locked = false;

    return ret;
}

/* Wire metadata never contains process-local pointers, references or GL state. */
typedef struct {
    uint64_t id, offset;
    int32_t width, height, stride;
    uint8_t format, type;
    uint16_t reserved;
} LorieBufferWire;
_Static_assert(sizeof(LorieBufferWire) == 32, "buffer wire layout");

__LIBC_HIDDEN__ bool LorieBuffer_sendHandleToUnixSocket(LorieBuffer* _Nonnull buffer, int socketFd) {
    if (socketFd < 0 || !buffer || (buffer->desc.type != LORIEBUFFER_FD &&
                                   buffer->desc.type != LORIEBUFFER_AHARDWAREBUFFER)) return false;
    LorieBufferWire wire = { .id = buffer->desc.id, .offset = buffer->offset,
        .width = buffer->desc.width, .height = buffer->desc.height, .stride = buffer->desc.stride,
        .format = buffer->desc.format, .type = buffer->desc.type };
    if (!lorieWriteFully(socketFd, &wire, sizeof(wire))) return false;
    if (buffer->desc.type == LORIEBUFFER_FD) return ancil_send_fd(socketFd, buffer->fd) == 0;
    if (__builtin_available(android 26, *))
        return AHardwareBuffer_sendHandleToUnixSocket(buffer->desc.buffer, socketFd) == 0;
    return false;
}

__LIBC_HIDDEN__ void LorieBuffer_recvHandleFromUnixSocket(int socketFd, LorieBuffer** outBuffer) {
    LorieBufferWire wire;
    LorieBuffer* buffer = NULL;
    size_t bytes;
    if (outBuffer) *outBuffer = NULL;
    if (!lorieReadFully(socketFd, &wire, sizeof(wire)) || wire.reserved ||
        !lorieBufferLayout(wire.width, wire.stride, wire.height, wire.offset, &bytes) ||
        (uint64_t)(off_t)wire.offset != wire.offset) return;
    if (wire.type == LORIEBUFFER_FD) {
        int fd = ancil_recv_fd(socketFd);
        if (fd < 0) return;
        buffer = allocate(wire.width, wire.stride, wire.height, wire.format, wire.type,
                          NULL, fd, (off_t)wire.offset, true);
    } else if (wire.type == LORIEBUFFER_AHARDWAREBUFFER && wire.offset == 0) {
        AHardwareBuffer* hardware = NULL;
        if (__builtin_available(android 26, *)) {
            if (AHardwareBuffer_recvHandleFromUnixSocket(socketFd, &hardware) != 0) return;
        }
        buffer = LorieBuffer_wrapAHardwareBuffer(hardware);
        if (buffer && (buffer->desc.width != wire.width || buffer->desc.height != wire.height ||
                       buffer->desc.stride != wire.stride || buffer->desc.format != wire.format)) {
            LorieBuffer_release(buffer);
            return;
        }
    }
    if (buffer) buffer->desc.id = wire.id;
    if (outBuffer) *outBuffer = buffer;
    else LorieBuffer_release(buffer);
}

__LIBC_HIDDEN__ void LorieBuffer_attachToGL(LorieBuffer* buffer) {
    const EGLint imageAttributes[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    if (!eglGetCurrentDisplay() || !buffer)
        return;

    if (buffer->image == NULL && buffer->desc.buffer && eglGetNativeClientBufferANDROID)
        buffer->image = eglCreateImageKHR(eglGetCurrentDisplay(), EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, eglGetNativeClientBufferANDROID(buffer->desc.buffer), imageAttributes);

    glGenTextures(1, &buffer->id);
    glBindTexture(GL_TEXTURE_2D, buffer->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (buffer->image)
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, buffer->image);
    else if (buffer->desc.data && buffer->desc.width > 0 && buffer->desc.height > 0) {
        int format = buffer->desc.format == AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM ? GL_BGRA_EXT : GL_RGBA;
        // The image will be updated in redraw call because of `drawRequested` flag, so we are not uploading pixels
        glTexImage2D(GL_TEXTURE_2D, 0, format, buffer->desc.stride, buffer->desc.height, 0, format, GL_UNSIGNED_BYTE, NULL);
    }
}

__LIBC_HIDDEN__ void LorieBuffer_bindTexture(LorieBuffer *buffer) {
    if (!buffer)
        return;

    glBindTexture(GL_TEXTURE_2D, buffer->id);
    if (buffer->desc.type == LORIEBUFFER_FD) {
        int format = buffer->desc.format == AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM ? GL_BGRA_EXT : GL_RGBA;
        int rows = buffer->desc.height - 1;
        if (rows) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, buffer->desc.stride, rows,
                                 format, GL_UNSIGNED_BYTE, buffer->desc.data);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, rows, buffer->desc.width, 1, format, GL_UNSIGNED_BYTE,
                       (char*)buffer->desc.data + (size_t)rows * buffer->desc.stride * 4);
    }
}

__LIBC_HIDDEN__ unsigned int LorieBuffer_getGLTextureId(LorieBuffer *buffer) {
    return buffer ? buffer->id : 0;
}

__LIBC_HIDDEN__ bool LorieBuffer_isRgba(LorieBuffer *buffer) {
    return LorieBuffer_description(buffer)->format != AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM;
}

__LIBC_HIDDEN__ void LorieBuffer_addToList(LorieBuffer* _Nullable buffer, struct xorg_list* _Nullable list) {
    if (buffer && list) {
        xorg_list_del(&buffer->link);
        xorg_list_add(&buffer->link, list);
    }
}

__LIBC_HIDDEN__ void LorieBuffer_removeFromList(LorieBuffer* _Nullable buffer) {
    if (buffer)
        xorg_list_del(&buffer->link);
}

__LIBC_HIDDEN__ LorieBuffer* _Nullable LorieBufferList_first(struct xorg_list* _Nullable list) {
    return xorg_list_is_empty(list) ? NULL : xorg_list_first_entry(list, LorieBuffer, link);
}

__LIBC_HIDDEN__ LorieBuffer* _Nullable LorieBufferList_findById(struct xorg_list* _Nullable list, uint64_t id) {
    LorieBuffer *buffer;
    xorg_list_for_each_entry(buffer, list, link)
        if (buffer->desc.id == id)
            return buffer;
    return NULL;
}

int LorieBuffer_recvAHardwareBufferHandleFromUnixSocket(int socketFd, AHardwareBuffer** outBuffer) {
    if (__builtin_available(android 26, *))
        return AHardwareBuffer_recvHandleFromUnixSocket(socketFd, outBuffer);
    return -ENOSYS;
}

void LorieBuffer_describeAHardwareBuffer(AHardwareBuffer* buffer, AHardwareBuffer_Desc* outDesc) {
    if (__builtin_available(android 26, *))
        AHardwareBuffer_describe(buffer, outDesc);
}

__LIBC_HIDDEN__ int ancil_send_fd(int sock, int fd) {
    char nothing = '!';
    struct iovec nothing_ptr = { .iov_base = &nothing, .iov_len = 1 };

    struct {
        struct cmsghdr align;
        int fd[1];
    } ancillary_data_buffer;

    struct msghdr message_header = {
            .msg_name = NULL,
            .msg_namelen = 0,
            .msg_iov = &nothing_ptr,
            .msg_iovlen = 1,
            .msg_flags = 0,
            .msg_control = &ancillary_data_buffer,
            .msg_controllen = sizeof(struct cmsghdr) + sizeof(int)
    };

#pragma clang diagnostic push
#pragma ide diagnostic ignored "NullDereference"
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&message_header);
    cmsg->cmsg_len = message_header.msg_controllen; // sizeof(int);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    ((int*) CMSG_DATA(cmsg))[0] = fd;
#pragma clang diagnostic pop

    ssize_t result;
    do { result = sendmsg(sock, &message_header, MSG_NOSIGNAL); } while (result < 0 && errno == EINTR);
    return result == 1 ? 0 : -1;
}

__LIBC_HIDDEN__ int ancil_recv_fd(int sock) {
    char nothing = 0;
    struct iovec nothing_ptr = { .iov_base = &nothing, .iov_len = 1 };

    union {
        struct cmsghdr align;
        char data[CMSG_SPACE(sizeof(int) * 2)];
    } ancillary_data_buffer = {0};

    struct msghdr message_header = {
            .msg_name = NULL,
            .msg_namelen = 0,
            .msg_iov = &nothing_ptr,
            .msg_iovlen = 1,
            .msg_flags = 0,
            .msg_control = &ancillary_data_buffer,
            .msg_controllen = sizeof(ancillary_data_buffer)
    };

    ssize_t result;
    do { result = recvmsg(sock, &message_header, MSG_CMSG_CLOEXEC); } while (result < 0 && errno == EINTR);
    if (result < 0) return -1;
    int received = -1, count = 0;
    for (struct cmsghdr* cmsg = CMSG_FIRSTHDR(&message_header); cmsg; cmsg = CMSG_NXTHDR(&message_header, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS || cmsg->cmsg_len < CMSG_LEN(0)) continue;
        size_t num = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        int* fds = (int*)CMSG_DATA(cmsg);
        for (size_t i = 0; i < num; ++i) {
            if (count++ == 0) received = fds[i];
            else close(fds[i]);
        }
    }
    if (result != 1 || nothing != '!' || count != 1 || (message_header.msg_flags & MSG_CTRUNC)) {
        if (received >= 0) close(received);
        return -1;
    }
    return received;
}
