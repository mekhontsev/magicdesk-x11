#pragma once

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

typedef bool (*LoriePeerAlive)(void* context);

// Bionic has no robust mutexes. Loss of a peer fails the session, never resets a
// mutex that another thread/process may still own. The owner supplies liveness.
static inline int lorieLockShared(pthread_mutex_t* mutex, LoriePeerAlive alive, void* context) {
    for (;;) {
        struct timespec deadline;
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        // Cancellation observation interval, not a deadline for GPU completion.
        deadline.tv_nsec += 33000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_nsec -= 1000000000L;
            deadline.tv_sec++;
        }
        int error = pthread_mutex_clocklock(mutex, CLOCK_MONOTONIC, &deadline);
        if (error != ETIMEDOUT) return error;
        if (!alive(context)) return EPIPE;
    }
}
