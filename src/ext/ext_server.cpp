// TCP server for the extension API.
// Localhost only, no encryption, no compression.

#include "ext_server.h"

#include <cstring>
#include <algorithm>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
  #define SHUT_RDWR SD_BOTH
  #define EXT_CLOSE closesocket
  #define EXT_ERRNO WSAGetLastError()
  #define EXT_WOULDBLOCK WSAEWOULDBLOCK
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <poll.h>
  #define EXT_CLOSE close
  #define EXT_ERRNO errno
  #define EXT_WOULDBLOCK EWOULDBLOCK
#endif

namespace ext {

Server::Server() : listen_fd_(-1), running_(false) {}

Server::~Server() {
    Stop();
}

bool Server::Start(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    listen_fd_ = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        EXT_CLOSE(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 4) < 0) {
        EXT_CLOSE(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    SetNonBlocking(listen_fd_);
    running_ = true;
    return true;
}

void Server::Stop() {
    running_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& c : clients_) {
        if (c.fd >= 0) {
            shutdown(c.fd, SHUT_RDWR);
            EXT_CLOSE(c.fd);
        }
    }
    clients_.clear();
    if (listen_fd_ >= 0) {
        EXT_CLOSE(listen_fd_);
        listen_fd_ = -1;
    }
#ifdef _WIN32
    WSACleanup();
#endif
}

void Server::AcceptClients() {
    if (listen_fd_ < 0) return;

    struct sockaddr_in addr{};
    socklen_t addrlen = sizeof(addr);
    int fd = (int)accept(listen_fd_, (struct sockaddr*)&addr, &addrlen);
    if (fd < 0) return;

    // TCP_NODELAY for low latency
    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));
    SetNonBlocking(fd);

    Client client{};
    client.fd = fd;
    client.subscriptions = 0;
    client.sequence = 0;
    client.alive = true;

    std::lock_guard<std::mutex> lock(mutex_);
    clients_.push_back(client);
}

void Server::ReadInput() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& c : clients_) {
        if (!c.alive) continue;
        HandleClientMessage(c);
    }
    RemoveDeadClients();
}

void Server::HandleClientMessage(Client& client) {
    // Try to read a frame header (non-blocking)
    FrameHeader fh;
    uint8_t buf[16];

    // Peek at available data
    int n = recv(client.fd, (char*)&fh, sizeof(fh), MSG_PEEK);
    if (n <= 0) {
        if (n == 0) client.alive = false; // Disconnected
        return;
    }
    if (n < (int)sizeof(fh)) return; // Incomplete header

    // Now actually read the header
    if (!RecvExact(client.fd, &fh, sizeof(fh))) {
        client.alive = false;
        return;
    }

    if (fh.magic != MAGIC) {
        client.alive = false;
        return;
    }

    // Read message header
    MsgHeader mh;
    if (fh.length < sizeof(mh)) {
        client.alive = false;
        return;
    }
    if (!RecvExact(client.fd, &mh, sizeof(mh))) {
        client.alive = false;
        return;
    }

    uint32_t payload_len = fh.length - sizeof(mh);

    // Read payload. Eight bytes covered every fixed-field command;
    // MSG_SET_JOBS carries a list, and its worst case is one entry
    // per pop of a full colony — s_colony::pop[42] (orion2.h:497) —
    // so 3 + 42*2 = 87. 256 is that with room, and the oversize
    // branch below still drops the client rather than reading on.
    uint8_t payload[256]{};
    if (payload_len > 0) {
        if (payload_len > sizeof(payload)) {
            client.alive = false;
            return;
        }
        if (!RecvExact(client.fd, payload, payload_len)) {
            client.alive = false;
            return;
        }
    }

    // Handle message
    switch (mh.msg_type) {
    case MSG_HELLO: {
        // Payload: uint16_t version, uint16_t subscriptions
        if (payload_len >= 4) {
            uint16_t sub;
            memcpy(&sub, payload + 2, 2);
            client.subscriptions = sub;
        }
        // Send HELLO_REPLY
        uint16_t reply[2] = { PROTO_VERSION, 0 };
        SendFrame(client.fd, MSG_HELLO_REPLY, 0,
                  client.sequence++, (uint8_t*)reply, sizeof(reply));
        // OrionLayer: a (re)connecting client needs the current field
        // list right away, not on the next count change.
        fields_dirty_ = true;
        break;
    }
    case MSG_ACTIVATE: {
        if (payload_len >= 2) {
            InputCommand cmd{};
            cmd.type = MSG_ACTIVATE;
            memcpy(&cmd.param1, payload, 2);
            // OrionLayer, open fix 62. Optional: int16 x, y, x_end, y_end of the field as the client saw it.
            if (payload_len >= 10) {
                cmd.pairs.assign(payload + 2, payload + 10);
            }
            input_queue_.push_back(cmd);
        }
        break;
    }
    case MSG_INJECT_KEY: {
        if (payload_len >= 2) {
            InputCommand cmd{};
            cmd.type = MSG_INJECT_KEY;
            memcpy(&cmd.param1, payload, 2);
            input_queue_.push_back(cmd);
        }
        break;
    }
    case MSG_INJECT_CLICK: {
        if (payload_len >= 4) {
            InputCommand cmd{};
            cmd.type = MSG_INJECT_CLICK;
            memcpy(&cmd.param1, payload, 2);
            memcpy(&cmd.param2, payload + 2, 2);
            input_queue_.push_back(cmd);
        }
        break;
    }
    case MSG_INJECT_RIGHT_CLICK: {
        // OrionLayer, open fix 61. int16 x, int16 y; shorter is dropped, never guessed.
        if (payload_len >= 4) {
            InputCommand cmd{};
            cmd.type = MSG_INJECT_RIGHT_CLICK;
            memcpy(&cmd.param1, payload, 2);
            memcpy(&cmd.param2, payload + 2, 2);
            input_queue_.push_back(cmd);
        }
        break;
    }
    case MSG_COMBAT_COMMAND: {
        // OrionLayer, open fix 58. The eleven bytes whole, or nothing.
        if (payload_len >= 11) {
            InputCommand cmd{};
            cmd.type = MSG_COMBAT_COMMAND;
            cmd.pairs.assign(payload, payload + 11);
            input_queue_.push_back(cmd);
        }
        break;
    }
    case MSG_INJECT_RAW_KEY: {
        // OrionLayer, open fix 89. Only the codes platform.cpp makes for F1-F10, Alt+F1-F10 and Alt+letter.
        if (payload_len >= 2) {
            uint16_t code = 0;
            memcpy(&code, payload, 2);
            const uint16_t scan = (uint16_t)(code >> 8);
            const bool fkey = (code & 0xFF) == 0 && scan >= 0x3B && scan <= 0x44;
            const bool alt_fkey = (code & 0xFF) == 0 && scan >= 0x68 && scan <= 0x71;
            const bool alt_letter = (code & 0xFF) == 0 &&
                ((scan >= 0x10 && scan <= 0x19) || (scan >= 0x1E && scan <= 0x26) ||
                 (scan >= 0x2C && scan <= 0x32));
            if (fkey || alt_fkey || alt_letter) {
                InputCommand cmd{};
                cmd.type = MSG_INJECT_RAW_KEY;
                cmd.param1 = (int16_t)code;
                input_queue_.push_back(cmd);
            }
        }
        break;
    }
    case MSG_SET_SPIES: {
        // OrionLayer, open fix 64. The whole list (2 + 3n bytes, n <= 8), or nothing.
        if (payload_len >= 2 && payload[0] <= 8 && payload_len == (size_t)2 + 3 * payload[0]) {
            InputCommand cmd{};
            cmd.type = MSG_SET_SPIES;
            cmd.pairs.assign(payload, payload + payload_len);
            input_queue_.push_back(cmd);
        }
        break;
    }
    case MSG_CANCEL_FIELD: {
        if (payload_len >= 2) {
            InputCommand cmd{};
            cmd.type = MSG_CANCEL_FIELD;
            memcpy(&cmd.param1, payload, 2);
            input_queue_.push_back(cmd);
        }
        break;
    }
    case MSG_SELECT_SHIP: {
        // int16 ship_idx, uint8 selected (open fix 21). Shorter is
        // dropped here rather than queued with a guessed value.
        if (payload_len >= 3) {
            InputCommand cmd{};
            cmd.type = MSG_SELECT_SHIP;
            memcpy(&cmd.param1, payload, 2);
            cmd.param2 = payload[2] != 0 ? 1 : 0;
            input_queue_.push_back(cmd);
        }
        break;
    }
    case MSG_SHOW_WINDOW: {
        // OrionLayer, open fix 43. uint8 show; shorter is dropped, never guessed.
        if (payload_len >= 1) {
            InputCommand cmd{};
            cmd.type = MSG_SHOW_WINDOW;
            cmd.param1 = payload[0] != 0 ? 1 : 0;
            input_queue_.push_back(cmd);
        }
        break;
    }
    case MSG_SET_JOBS: {
        // int16 colony_idx, uint8 count, count x (pop_idx, job).
        // A malformed list is DROPPED here rather than half-queued:
        // the command is all-or-nothing (fundament 52) and the
        // cheapest place to hold that is before it is a command.
        if (payload_len >= 3) {
            const uint8_t count = payload[2];
            if (count > 0 && count <= 42 &&
                    (size_t)3 + (size_t)count * 2 <= payload_len) {
                InputCommand cmd{};
                cmd.type = MSG_SET_JOBS;
                memcpy(&cmd.param1, payload, 2);
                cmd.param2 = (int16_t)count;
                cmd.pairs.assign(payload + 3,
                                 payload + 3 + (size_t)count * 2);
                input_queue_.push_back(cmd);
            }
        }
        break;
    }
    default:
        break;
    }
}

void Server::SendToSubscribers(uint16_t sub_flag, uint16_t msg_type,
                                const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& c : clients_) {
        if (!c.alive) continue;
        if (!(c.subscriptions & sub_flag)) continue;
        if (!SendFrame(c.fd, msg_type, 0, c.sequence++, data, len)) {
            c.alive = false;
        }
    }
    RemoveDeadClients();
}

bool Server::PopInput(InputCommand& cmd) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (input_queue_.empty()) return false;
    cmd = input_queue_.front();
    input_queue_.erase(input_queue_.begin());
    return true;
}

int Server::ClientCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (int)clients_.size();
}

bool Server::SendFrame(int fd, uint16_t msg_type, uint16_t flags,
                        uint32_t seq, const uint8_t* data, size_t len) {
    FrameHeader fh;
    fh.magic = MAGIC;
    fh.length = (uint32_t)(sizeof(MsgHeader) + len);

    MsgHeader mh;
    mh.msg_type = msg_type;
    mh.flags = flags;
    mh.sequence = seq;

    // Send as one buffer to reduce syscalls
    size_t total = sizeof(fh) + sizeof(mh) + len;
    std::vector<uint8_t> buf(total);
    memcpy(buf.data(), &fh, sizeof(fh));
    memcpy(buf.data() + sizeof(fh), &mh, sizeof(mh));
    if (len > 0 && data != nullptr) {
        memcpy(buf.data() + sizeof(fh) + sizeof(mh), data, len);
    }

    // The socket is non-blocking. When the client is momentarily busy
    // (asset loading, screen transition) its receive window fills and
    // send() returns EWOULDBLOCK. Dropping the client for that is far
    // too aggressive — wait for the socket to drain, up to a bounded
    // total per frame, and only give up on a real error or a stall.
    constexpr int kWaitSliceMs = 50;
    constexpr int kMaxWaitMs = 1000;
    int waited_ms = 0;

    size_t sent = 0;
    while (sent < total) {
        int n = send(fd, (const char*)buf.data() + sent,
                     (int)(total - sent), 0);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n == 0) return false;
        int err = EXT_ERRNO;
#ifndef _WIN32
        if (err == EINTR) continue;
#endif
        if (err != EXT_WOULDBLOCK
#ifndef _WIN32
            && err != EAGAIN
#endif
        ) {
            return false;
        }
        if (waited_ms >= kMaxWaitMs) return false;   // stalled client
#ifdef _WIN32
        fd_set wfds; FD_ZERO(&wfds); FD_SET((SOCKET)fd, &wfds);
        timeval tv{0, kWaitSliceMs * 1000};
        select(0, nullptr, &wfds, nullptr, &tv);
#else
        struct pollfd pfd{fd, POLLOUT, 0};
        poll(&pfd, 1, kWaitSliceMs);
#endif
        waited_ms += kWaitSliceMs;
    }
    return true;
}

bool Server::RecvExact(int fd, void* buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        int n = recv(fd, (char*)buf + got, (int)(len - got), 0);
        if (n <= 0) return false;
        got += (size_t)n;
    }
    return true;
}

void Server::RemoveDeadClients() {
    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(),
            [](const Client& c) {
                if (!c.alive && c.fd >= 0) {
                    shutdown(c.fd, SHUT_RDWR);
                    EXT_CLOSE(c.fd);
                }
                return !c.alive;
            }),
        clients_.end()
    );
}

void Server::SetNonBlocking(int fd) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

} // namespace ext
