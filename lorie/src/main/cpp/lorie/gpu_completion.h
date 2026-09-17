#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>

static inline bool lorieNotifyGpuCompletion(int fd) {
    uint64_t value = 1;
    ssize_t result;
    do { result = write(fd, &value, sizeof(value)); } while (result < 0 && errno == EINTR);
    /* A saturated nonblocking eventfd already has a pending notification. */
    return result == sizeof(value) || (result < 0 && errno == EAGAIN);
}

static inline bool lorieConsumeGpuCompletion(int fd) {
    uint64_t value;
    ssize_t result;
    do { result = read(fd, &value, sizeof(value)); } while (result < 0 && errno == EINTR);
    return result == sizeof(value);
}
