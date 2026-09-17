#define _GNU_SOURCE
#include "../lorie/src/main/cpp/lorie/shared_lock.h"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    pthread_mutex_t pixels, gate;
    pthread_cond_t observed;
    bool alive, checked, released, acquired;
    int result;
} Fixture;

static void init(pthread_mutex_t* mutex) {
    pthread_mutexattr_t attr;
    assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED));
    assert(!pthread_mutex_init(mutex, &attr));
    pthread_mutexattr_destroy(&attr);
}

static bool alive(void* context) {
    Fixture* fixture = context;
    pthread_mutex_lock(&fixture->gate);
    fixture->checked = true;
    pthread_cond_signal(&fixture->observed);
    bool result = fixture->alive;
    pthread_mutex_unlock(&fixture->gate);
    return result;
}

static void* contender(void* context) {
    Fixture* fixture = context;
    fixture->result = lorieLockShared(&fixture->pixels, alive, fixture);
    if (!fixture->result) {
        pthread_mutex_lock(&fixture->gate);
        assert(fixture->released);
        fixture->acquired = true;
        pthread_mutex_unlock(&fixture->gate);
        assert(!pthread_mutex_unlock(&fixture->pixels));
    }
    return NULL;
}

static void contention(bool connected) {
    Fixture fixture = {.gate = PTHREAD_MUTEX_INITIALIZER, .observed = PTHREAD_COND_INITIALIZER,
            .alive = connected};
    init(&fixture.pixels);
    assert(!lorieLockShared(&fixture.pixels, alive, &fixture));
    assert(!lorieLockShared(&fixture.pixels, alive, &fixture));
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, contender, &fixture));
    pthread_mutex_lock(&fixture.gate);
    while (!fixture.checked) pthread_cond_wait(&fixture.observed, &fixture.gate);
    assert(!fixture.acquired);
    fixture.released = true;
    pthread_mutex_unlock(&fixture.gate);
    if (!connected) {
        pthread_join(thread, NULL);
        assert(fixture.result == EPIPE);
    }
    // A timed-out/cancelled waiter must not reinitialize or release our lock.
    assert(!pthread_mutex_unlock(&fixture.pixels));
    assert(!pthread_mutex_unlock(&fixture.pixels));
    if (connected) {
        pthread_join(thread, NULL);
        assert(!fixture.result && fixture.acquired);
    }
    pthread_mutex_destroy(&fixture.pixels);
    pthread_mutex_destroy(&fixture.gate);
    pthread_cond_destroy(&fixture.observed);
}

static bool dead(void* unused) { (void)unused; return false; }

int main(void) {
    contention(true);
    contention(false);
    pthread_mutex_t* mutex = mmap(NULL, sizeof(*mutex), PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(mutex != MAP_FAILED);
    init(mutex);
    pid_t child = fork();
    assert(child >= 0);
    if (!child) { assert(!pthread_mutex_lock(mutex)); _exit(0); }
    int status;
    assert(waitpid(child, &status, 0) == child && status == 0);
    assert(lorieLockShared(mutex, dead, NULL) == EPIPE);
    munmap(mutex, sizeof(*mutex));
    puts("shared lock: contention, recursion, cancellation and dead peer passed");
}
