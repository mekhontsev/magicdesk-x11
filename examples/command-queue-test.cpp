#include "../lorie/src/main/cpp/lorie/command_queue.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>

static size_t descriptorCount() {
    size_t count = 0;
    for (int fd = 0; fd < 1024; fd++) if (fcntl(fd, F_GETFD) >= 0) count++;
    return count;
}

int main() {
    int sockets[2], pipefd[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets));
    assert(!pipe(pipefd));
    assert(write(pipefd[1], "X", 1) == 1);
    close(pipefd[1]);
    int size = 1024;
    setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    std::vector<char> payload(65536, 'a'), tail(71, 'b');
    LorieCommandQueue queue;
    assert(queue.append(payload.data(), payload.size(), pipefd[0]));
    close(pipefd[0]); // The queue, not its caller, owns the descriptor now.
    assert(queue.append(tail.data(), tail.size()));
    assert(queue.flush(sockets[0]) && !queue.empty());
    size_t received = 0;
    while (received < payload.size()) {
        assert(queue.flush(sockets[0]));
        char bytes[193];
        ssize_t count = recv(sockets[1], bytes,
                std::min(sizeof(bytes), payload.size() - received), MSG_DONTWAIT);
        if (count < 0) { assert(errno == EAGAIN); continue; }
        assert(count > 0);
        for (ssize_t i = 0; i < count; i++) assert(bytes[i] == 'a');
        received += count;
    }
    assert(queue.flush(sockets[0]));
    char marker;
    iovec payloadFd{&marker, 1};
    alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))]{};
    msghdr message{};
    message.msg_iov = &payloadFd; message.msg_iovlen = 1;
    message.msg_control = control; message.msg_controllen = sizeof(control);
    assert(recvmsg(sockets[1], &message, MSG_DONTWAIT) == 1 && marker == '!');
    cmsghdr* rights = CMSG_FIRSTHDR(&message);
    assert(rights && rights->cmsg_type == SCM_RIGHTS);
    int descriptor;
    memcpy(&descriptor, CMSG_DATA(rights), sizeof(descriptor));
    assert(read(descriptor, &marker, 1) == 1 && marker == 'X');
    close(descriptor);
    char after[71];
    assert(queue.flush(sockets[0]) && queue.empty());
    assert(recv(sockets[1], after, sizeof(after), MSG_DONTWAIT) == sizeof(after));
    assert(!memcmp(after, tail.data(), tail.size()));

    size_t before = descriptorCount();
    for (int i = 0; i < 16; i++) assert(queue.append(payload.data(), payload.size(), sockets[1]));
    assert(!queue.append("x", 1)); // Bounded admission; never silently drop a key/FD.
    queue.clear();
    assert(descriptorCount() == before);
    assert(queue.append("x", 1));
    close(sockets[1]);
    assert(!queue.flush(sockets[0]));
    close(sockets[0]);
    puts("command queue: backpressure, ordered partial writes, FD lifetime and disconnect passed");
}
