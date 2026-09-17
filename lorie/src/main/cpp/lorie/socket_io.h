#pragma once

#include <stdbool.h>
#include <errno.h>
#include <sys/socket.h>

static inline bool lorieReadFully(int fd, void* data, size_t size) {
    char* cursor = (char*)data;
    while (size) {
        ssize_t count = recv(fd, cursor, size, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        cursor += count;
        size -= (size_t)count;
    }
    return true;
}

static inline bool lorieWriteFully(int fd, const void* data, size_t size) {
    const char* cursor = (const char*)data;
    while (size) {
        ssize_t count = send(fd, cursor, size, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        cursor += count;
        size -= (size_t)count;
    }
    return true;
}
