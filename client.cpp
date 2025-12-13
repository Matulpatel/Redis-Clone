#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>


static void exit_with_message(const char *message) {
    int error_number = errno;
    fprintf(stderr, "[%d] %s\n", error_number, message);
    abort();
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

    char msg[] = "hello";
    write(file_descriptor, msg, strlen(msg));

    char rbuf[64] = {};
    ssize_t response = read(file_descriptor, rbuf, sizeof(rbuf) - 1);
    if (response < 0) {
        exit_with_message("reading error");
    }
    printf("server says: %s\n", rbuf);
    
    close(file_descriptor);
    return 0;
}
