#include <cstdlib>
#include <cstdint>
#include <pthread.h>

extern "C" {
#include <dix-config.h>
#include <misc.h>
#include <dix.h>
#include <dixstruct.h>
extern WorkQueuePtr workQueue;
}

namespace {
struct WorkItem {
    Bool (*function)(ClientPtr, void *);
    ClientPtr client;
    void *closure;
    uint64_t sequence;
    WorkItem *next;
};

WorkQueueRec sentinel;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
WorkItem *head;
WorkItem **tail = &head;
uint64_t sequence;

__attribute__((constructor)) void initWorkQueue() {
    // WaitFor.c tests this pointer before calling ProcessWorkQueue.
    workQueue = &sentinel;
}

void append(WorkItem *item) {
    pthread_mutex_lock(&lock);
    item->sequence = ++sequence;
    item->next = nullptr;
    *tail = item;
    tail = &item->next;
    pthread_mutex_unlock(&lock);
}

uint64_t lastSequence() {
    pthread_mutex_lock(&lock);
    uint64_t last = sequence;
    pthread_mutex_unlock(&lock);
    return last;
}

WorkItem *take(uint64_t last, bool zombiesOnly) {
    pthread_mutex_lock(&lock);
    WorkItem **cursor = &head;
    while (*cursor && (*cursor)->sequence <= last && zombiesOnly &&
           (!(*cursor)->client || !(*cursor)->client->clientGone)) cursor = &(*cursor)->next;
    WorkItem *item = *cursor;
    if (item && item->sequence <= last) {
        *cursor = item->next;
        if (!*cursor) tail = cursor;
    } else item = nullptr;
    pthread_mutex_unlock(&lock);
    return item;
}
}

extern "C" {
Bool QueueWorkProc(Bool (*function)(ClientPtr, void *), ClientPtr client, void *closure) {
    auto *item = (WorkItem *)malloc(sizeof(WorkItem));
    if (!item) return FALSE;
    *item = {function, client, closure, 0, nullptr};
    append(item);
    return TRUE;
}

void ProcessWorkQueue(void) {
    const uint64_t last = lastSequence();
    // New work and retries belong to the next pass. Callbacks may enqueue work
    // or close clients (which invokes ProcessWorkQueueZombies recursively).
    while (WorkItem *item = take(last, false)) {
        if (item->function(item->client, item->closure)) free(item);
        else append(item);
    }
}

void ProcessWorkQueueZombies(void) {
    const uint64_t last = lastSequence();
    while (WorkItem *item = take(last, true)) {
        item->function(item->client, item->closure);
        free(item);
    }
}

void ClearWorkQueue(void) {
    const uint64_t last = lastSequence();
    while (WorkItem *item = take(last, false)) free(item);
}
}
