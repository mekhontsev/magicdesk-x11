#include <cassert>
#include <cstdio>
#include <vector>
#include <thread>
#include "../lorie/src/main/cpp/lorie/workqueue.cpp"
extern "C" { WorkQueuePtr workQueue; }

static std::vector<int> order;
static Bool record(ClientPtr, void* closure) {
    int id = (intptr_t)closure;
    order.push_back(id);
    if (id == 0) {
        assert(QueueWorkProc(record, nullptr, (void*)129));
        assert(QueueWorkProc(record, nullptr, (void*)130));
    }
    return TRUE;
}

static int retries;
static Bool retry(ClientPtr, void*) { return ++retries == 2; }
static Bool zombie(ClientPtr, void*) {
    assert(QueueWorkProc(record, nullptr, (void*)500));
    return FALSE;
}
static ClientRec gone;
static Bool closeClient(ClientPtr, void*) {
    gone.clientGone = TRUE;
    ProcessWorkQueueZombies();
    return TRUE;
}

int main() {
    for (int i = 0; i <= 128; ++i) assert(QueueWorkProc(record, nullptr, (void*)(intptr_t)i));
    ProcessWorkQueue();
    assert(order.size() == 129);
    ProcessWorkQueue();
    assert(order.size() == 131);
    for (int i = 0; i <= 130; ++i) assert(order[i] == i);
    assert(QueueWorkProc(retry, nullptr, nullptr));
    ProcessWorkQueue(); assert(retries == 1);
    ProcessWorkQueue(); assert(retries == 2);
    ProcessWorkQueue(); assert(retries == 2);

    order.clear();
    assert(QueueWorkProc(closeClient, nullptr, nullptr));
    assert(QueueWorkProc(record, nullptr, (void*)400));
    assert(QueueWorkProc(zombie, &gone, nullptr));
    ProcessWorkQueue();
    assert(order == std::vector<int>{400});
    ProcessWorkQueue();
    assert((order == std::vector<int>{400, 500}));

    order.clear();
    std::thread producers[4];
    for (int p = 0; p < 4; ++p) producers[p] = std::thread([p] {
        for (int n = 1; n <= 2000; ++n)
            assert(QueueWorkProc(record, nullptr, (void*)(intptr_t)(p * 10000 + n)));
    });
    for (auto& producer : producers) producer.join();
    ProcessWorkQueue();
    assert(order.size() == 8000);
    int last[4] = {};
    for (int id : order) {
        int p = id / 10000;
        assert(id % 10000 == ++last[p]);
    }
    assert(QueueWorkProc(record, nullptr, (void*)600));
    ClearWorkQueue(); ProcessWorkQueue(); assert(order.size() == 8000);
    puts("X11 FIFO ordering, retries, concurrent producers and zombie reentrancy passed");
}
