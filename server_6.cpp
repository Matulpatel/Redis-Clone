#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <vector>


// print simple message
static void log_msg(const char *text) {
    fprintf(stderr, "%s\n", text);
}

// print message with errno
static void log_errno(const char *text) {
    fprintf(stderr, "[errno:%d] %s\n", errno, text);
}

// fatal error
static void fatal(const char *text) {
    fprintf(stderr, "[%d] %s\n", errno, text);
    abort();
}

// set fd to non-blocking mode
static void set_nonblocking(int fd) {
    errno = 0;
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno) {
        fatal("fcntl get error");
        return;
    }

    flags |= O_NONBLOCK;

    errno = 0;
    (void)fcntl(fd, F_SETFL, flags);
    if (errno) {
        fatal("fcntl set error");
    }
}

const size_t MAX_MSG_SIZE = 32 << 20;  // larger than typical kernel buffer

struct Connection {
    int fd = -1;
    // desired events for poll()
    bool want_read = false;
    bool want_write = false;
    bool want_close = false;
    // IO buffers
    std::vector<uint8_t> in_buf;   // incoming raw data
    std::vector<uint8_t> out_buf;  // pending responses
};

// append data at end of buffer
static void
buffer_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

// remove bytes from front of buffer
static void buffer_consume(std::vector<uint8_t> &buf, size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}

// accept new client connection
static Connection *handle_accept(int listen_fd) {
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addrlen);
    if (client_fd < 0) {
        log_errno("accept() error");
        return NULL;
    }

    uint32_t ip = client_addr.sin_addr.s_addr;
    fprintf(stderr, "new client %u.%u.%u.%u:%u\n",
        ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
        ntohs(client_addr.sin_port)
    );

    // make socket non-blocking
    set_nonblocking(client_fd);

    Connection *conn = new Connection();
    conn->fd = client_fd;
    conn->want_read = true;
    return conn;
}

// attempt to process one complete request
static bool try_one_request(Connection *conn) {
    // need header first
    if (conn->in_buf.size() < 4) {
        return false;   // wait for more data
    }

    uint32_t msg_len = 0;
    memcpy(&msg_len, conn->in_buf.data(), 4);
    if (msg_len > MAX_MSG_SIZE) {
        log_msg("message too large");
        conn->want_close = true;
        return false;
    }

    // wait for full body
    if (4 + msg_len > conn->in_buf.size()) {
        return false;
    }

    const uint8_t *payload = &conn->in_buf[4];

    // application logic (echo)
    printf("client says: len:%d data:%.*s\n",
        msg_len, msg_len < 100 ? msg_len : 100, payload);

    // build response
    buffer_append(conn->out_buf, (const uint8_t *)&msg_len, 4);
    buffer_append(conn->out_buf, payload, msg_len);

    // consume request from input buffer
    buffer_consume(conn->in_buf, 4 + msg_len);
    return true;
}

// handle writable socket
static void handle_write(Connection *conn) {
    assert(conn->out_buf.size() > 0);

    ssize_t sent = write(conn->fd, conn->out_buf.data(), conn->out_buf.size());
    if (sent < 0 && errno == EAGAIN) {
        return; // not actually writable
    }
    if (sent < 0) {
        log_errno("write() error");
        conn->want_close = true;
        return;
    }

    buffer_consume(conn->out_buf, (size_t)sent);

    // update intent
    if (conn->out_buf.empty()) {
        conn->want_read = true;
        conn->want_write = false;
    }
}

// handle readable socket
static void handle_read(Connection *conn) {
    uint8_t tmp[64 * 1024];
    ssize_t got = read(conn->fd, tmp, sizeof(tmp));
    if (got < 0 && errno == EAGAIN) {
        return;
    }
    if (got < 0) {
        log_errno("read() error");
        conn->want_close = true;
        return;
    }
    if (got == 0) {
        if (conn->in_buf.empty()) {
            log_msg("client closed");
        } else {
            log_msg("unexpected EOF");
        }
        conn->want_close = true;
        return;
    }

    buffer_append(conn->in_buf, tmp, (size_t)got);

    // process as many requests as possible
    while (try_one_request(conn)) {}

    if (!conn->out_buf.empty()) {
        conn->want_read = false;
        conn->want_write = true;
        return handle_write(conn);
    }
}

int main() {
    // listening socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        fatal("socket()");
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = ntohl(0);  // 0.0.0.0

    int rc = bind(listen_fd, (const sockaddr *)&addr, sizeof(addr));
    if (rc) {
        fatal("bind()");
    }

    set_nonblocking(listen_fd);

    rc = listen(listen_fd, SOMAXCONN);
    if (rc) {
        fatal("listen()");
    }

    // fd -> connection mapping
    std::vector<Connection *> fd_map;
    std::vector<struct pollfd> poll_fds;

    // event loop
    while (true) {
        poll_fds.clear();

        // listening socket
        poll_fds.push_back({listen_fd, POLLIN, 0});

        // client sockets
        for (Connection *conn : fd_map) {
            if (!conn) continue;

            struct pollfd pfd = {conn->fd, POLLERR, 0};
            if (conn->want_read)  pfd.events |= POLLIN;
            if (conn->want_write) pfd.events |= POLLOUT;
            poll_fds.push_back(pfd);
        }

        int ready = poll(poll_fds.data(), (nfds_t)poll_fds.size(), -1);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) fatal("poll()");

        // accept new client
        if (poll_fds[0].revents) {
            if (Connection *conn = handle_accept(listen_fd)) {
                if (fd_map.size() <= (size_t)conn->fd) {
                    fd_map.resize(conn->fd + 1);
                }
                fd_map[conn->fd] = conn;
            }
        }

        // handle clients
        for (size_t i = 1; i < poll_fds.size(); ++i) {
            uint32_t events = poll_fds[i].revents;
            if (!events) continue;

            Connection *conn = fd_map[poll_fds[i].fd];

            if (events & POLLIN) {
                handle_read(conn);
            }
            if (events & POLLOUT) {
                handle_write(conn);
            }

            if ((events & POLLERR) || conn->want_close) {
                close(conn->fd);
                fd_map[conn->fd] = NULL;
                delete conn;
            }
        }
    }
    return 0;
}
