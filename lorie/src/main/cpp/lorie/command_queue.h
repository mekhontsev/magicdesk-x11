#pragma once

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

// Single Looper-owned stream writer. A header and its ancillary FD are one
// queued message; partial writes never interleave commands or borrow caller FDs.
class LorieCommandQueue {
    struct Message {
        Message* next;
        size_t size, sent;
        int descriptor;
        char bytes[];
    };
    Message *first = nullptr, *last = nullptr;
    size_t bytes = 0, count = 0;
public:
    static constexpr size_t MAX_BYTES = 1024 * 1024, MAX_MESSAGES = 4096;
    ~LorieCommandQueue() { clear(); }
    bool empty() const { return first == nullptr; }
    void clear() {
        while (first) {
            Message* next = first->next;
            if (first->descriptor >= 0) close(first->descriptor);
            free(first);
            first = next;
        }
        last = nullptr;
        bytes = count = 0;
    }
    bool append(const void* data, size_t size, int descriptor = -1) {
        if (size > MAX_BYTES - bytes || count == MAX_MESSAGES) return false;
        int copy = descriptor < 0 ? -1 : fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
        if (descriptor >= 0 && copy < 0) return false;
        auto* message = (Message*)malloc(sizeof(Message) + size);
        if (!message) { if (copy >= 0) close(copy); return false; }
        message->next = nullptr; message->size = size; message->sent = 0; message->descriptor = copy;
        memcpy(message->bytes, data, size);
        if (last) last->next = message;
        else first = message;
        last = message;
        count++;
        bytes += size;
        return true;
    }
    // EAGAIN is backpressure, not failure. The owner watches socket writability.
    bool flush(int socket) {
        while (first) {
            auto& message = *first;
            ssize_t count;
            if (message.sent < message.size) {
                count = send(socket, message.bytes + message.sent,
                        message.size - message.sent, MSG_NOSIGNAL | MSG_DONTWAIT);
                if (count > 0) { message.sent += count; continue; }
            } else if (message.descriptor >= 0) {
                char marker = '!';
                iovec payload{&marker, 1};
                alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))]{};
                msghdr header{};
                header.msg_iov = &payload;
                header.msg_iovlen = 1;
                header.msg_control = control;
                header.msg_controllen = sizeof(control);
                cmsghdr* rights = CMSG_FIRSTHDR(&header);
                rights->cmsg_level = SOL_SOCKET;
                rights->cmsg_type = SCM_RIGHTS;
                rights->cmsg_len = CMSG_LEN(sizeof(int));
                memcpy(CMSG_DATA(rights), &message.descriptor, sizeof(int));
                count = sendmsg(socket, &header, MSG_NOSIGNAL | MSG_DONTWAIT);
                if (count == 1) { close(message.descriptor); message.descriptor = -1; continue; }
            } else {
                bytes -= message.size;
                Message* next = first->next;
                free(first);
                first = next;
                if (!first) last = nullptr;
                this->count--;
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            return count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
        }
        return true;
    }
};
