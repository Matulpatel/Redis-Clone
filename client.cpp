#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <assert.h>



static void exit_with_message(const char *message) {
    int error_number = errno;
    fprintf(stderr, "[%d] %s\n", error_number, message);
    abort();
}

static void print_message(const char* message){
    fprintf(stderr, "%s\n", message);
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

static int32_t query(int file_descriptor, const char* text){
    uint32_t len_text = strlen(text);

    //check if the message si within limits
    if (len_text > max_msg_len) return -1;

    char write_buffer[len_text + 4];
    memcpy(write_buffer, &len_text, 4);
    memcpy(&(write_buffer[4]), text, len_text);
    if (int32_t error_code = write_all(file_descriptor, write_buffer, len_text+4)){
        return error_code;
    }

    char read_buffer[max_msg_len + 4];
    errno = 0;
    int32_t error_code = read_full(file_descriptor, read_buffer, 4);
    if (error_code){
        print_message(errno == 0 ? "EOF" : "read() error");
    }

    memcpy(&len_text, read_buffer, 4);
    if (len_text > max_msg_len){
        print_message("Text too long");
        return -1;
    }

    error_code = read_full(file_descriptor, read_buffer + 4, len_text);
    if (error_code){
        print_message("read() error");
        return error_code;
    }

    printf("Server says: %.*s\n", len_text, read_buffer + 4);
    return 0;
}

int main() {
    
    int file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (file_descriptor < 0) {
        exit_with_message("socket error");
    }

    struct sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = ntohs(1234);
    server_address.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); 
    int return_value = connect(file_descriptor, (const struct sockaddr *)&server_address, sizeof(server_address));
    if (return_value) {
        exit_with_message("connection error");
    }

    int32_t error_code = query(file_descriptor, "what we doin gng");
    if (error_code){
        goto L_DONE;
    }

    error_code = query(file_descriptor, "what we doin gng 2");
    if (error_code){
        goto L_DONE;
    }

    error_code = query(file_descriptor, "what we doin gng 3");
    if (error_code){
        goto L_DONE;
    }
    
 
L_DONE:
    close(file_descriptor);
    return 0;
}
