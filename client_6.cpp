#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <string>
#include <vector>

// print a message to stderr
static void log_msg(const char *text) {
    fprintf(stderr, "%s\n", text);
}

// print error and abort
static void fatal(const char *text) {
    int err_code = errno;
    fprintf(stderr, "[%d] %s\n", err_code, text);
    abort();
}

// read exactly n bytes from fd
static int32_t read_exact(int fd, uint8_t *buffer, size_t bytes) {
    while (bytes > 0) {
        ssize_t got = read(fd, buffer, bytes);
        if (got <= 0) {
            return -1;  // error or premature EOF
        }
        assert((size_t)got <= bytes);
        bytes -= (size_t)got;
        buffer += got;
    }
    return 0;
}

// write all bytes to fd
static int32_t write_exact(int fd, const uint8_t *buffer, size_t bytes) {
    while (bytes > 0) {
        ssize_t sent = write(fd, buffer, bytes);
        if (sent <= 0) {
            return -1;  // write error
        }
        assert((size_t)sent <= bytes);
        bytes -= (size_t)sent;
        buffer += sent;
    }
    return 0;
}

// append data to vector buffer
static void
append_buffer(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

// maximum allowed message size
const size_t MAX_MSG_SIZE = 32 << 20;

// send one request (length + payload)
static int32_t send_request(int fd, const uint8_t *payload, size_t len) {
    if (len > MAX_MSG_SIZE) {
        return -1;
    }

    std::vector<uint8_t> out_buf;
    append_buffer(out_buf, (const uint8_t *)&len, 4);
    append_buffer(out_buf, payload, len);
    return write_exact(fd, out_buf.data(), out_buf.size());
}

// read one response
static int32_t read_response(int fd) {
    // read 4-byte length header
    std::vector<uint8_t> in_buf(4);
    errno = 0;

    int32_t rc = read_exact(fd, in_buf.data(), 4);
    if (rc) {
        if (errno == 0) {
            log_msg("EOF");
        } else {
            log_msg("read error");
        }
        return rc;
    }

    uint32_t msg_len = 0;
    memcpy(&msg_len, in_buf.data(), 4);  // assume little-endian
    if (msg_len > MAX_MSG_SIZE) {
        log_msg("message too large");
        return -1;
    }

    // read response body
    in_buf.resize(4 + msg_len);
    rc = read_exact(fd, &in_buf[4], msg_len);
    if (rc) {
        log_msg("read error");
        return rc;
    }

    // process response
    printf("len:%u data:%.*s\n",
           msg_len,
           msg_len < 100 ? msg_len : 100,
           &in_buf[4]);
    return 0;
}

int main() {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        fatal("socket()");
    }

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = ntohs(1234);
    server_addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);  // 127.0.0.1

    int ret = connect(sock_fd,
                      (const struct sockaddr *)&server_addr,
                      sizeof(server_addr));
    if (ret) {
        fatal("connect()");
    }

    // send multiple pipelined requests
    std::vector<std::string> requests = {
        "hello1", "hello2", "hello3",
        // large payload to test partial IO
        std::string(MAX_MSG_SIZE, 'z'),
        "hello5",
    };

    for (const std::string &req : requests) {
        if (send_request(sock_fd,
                         (const uint8_t *)req.data(),
                         req.size())) {
            goto CLEANUP;
        }
    }

    for (size_t i = 0; i < requests.size(); ++i) {
        if (read_response(sock_fd)) {
            goto CLEANUP;
        }
    }

CLEANUP:
    close(sock_fd);
    return 0;
}
