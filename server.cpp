#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>

static void print_message(const char* message){
    fprintf(stderr, "%s\n", message);
}

static void exit_with_message(const char* message){
    int error_number = errno;
    fprintf(stderr, "[%d] %s\n", error_number, message);
    abort();
}

static int32_t read_full(int fd, char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = read(fd, buf, n);
        if (rv <= 0) {
            return -1;  // error or End of the file
        }
        n -= static_cast<ssize_t>(rv);
        buf += rv;
    }
    return 0;
}

static int32_t write_all(int fd, const char *buf, size_t n) {
    while (n > 0) {
        ssize_t rv = write(fd, buf, n);
        if (rv <= 0) {
            return -1;
        }
        n -= static_cast<ssize_t>(rv);
        buf += rv;
    }
    return 0;
}
const size_t max_msg_len = 4096;

static int32_t one_request(int connfd) {
    char buffer[4 + max_msg_len];

    // read the 4-byte length
    errno = 0;
    if (read_full(connfd, buffer, 4)) {
        print_message(errno == 0 ? "Client closed connection" : "read error");
        return -1;
    }

    uint32_t len = 0;
    memcpy(&len, buffer, 4);

    if (len > max_msg_len) {
        print_message("message too large");
        return -1;
    }

    //read message body
    if (read_full(connfd, buffer + 4, len)) {
        print_message("read error");
        return -1;
    }

    //process request
    fprintf(stderr, "Client says: %.*s\n", len, buffer + 4);

    //send reply
    const char reply[] = "Hey we are jobless ppl , starting the project (almost in the middle of the vacation)";
    uint32_t reply_len = strlen(reply);

    if (reply_len > max_msg_len) {
        print_message("reply too large");
        return -1;
    }

    memcpy(buffer, &reply_len, 4);
    memcpy(buffer + 4, reply, reply_len);

    return write_all(connfd, buffer, 4 + reply_len);
}

int main(){
    int file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (file_descriptor < 0){
        exit_with_message("Socket error");
    }

    //set socket options
    int val =1;
    setsockopt(file_descriptor, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    //bind to wildcar address
    struct sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(1234);
    server_address.sin_addr.s_addr = htonl(0);

    int is_error = bind(file_descriptor, (const sockaddr*) &server_address, sizeof(server_address));
    if (is_error){
        exit_with_message("Binding error");
    }

    is_error = listen(file_descriptor, SOMAXCONN);

    if (is_error){
        exit_with_message("Listening error");
    }

    while (true){
        struct sockaddr_in client_address = {};
        socklen_t address_length = sizeof(client_address);

        int connection_file_descriptor = accept(file_descriptor, (struct sockaddr*) &client_address, &address_length);

        if (connection_file_descriptor < 0){
            continue;
        }

        while (true) {
            int32_t err = one_request(connection_file_descriptor);
            if (err) {
                break;
            }
        }
        close(connection_file_descriptor);
    }

}
