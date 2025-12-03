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
    fprintf(stderr, "[%d] %s", error_number, message);
    abort();
}

static void do_something(int connection_file_descriptor){
    char read_buffer[64] = {};
    //signed ssize_t used to catch errors
    ssize_t length = read(connection_file_descriptor, read_buffer, sizeof(read_buffer) - 1);
    if (length < 0){
        print_message("read() error..");
    }
    fprintf(stderr, "Client message : %s", read_buffer);

    char write_buffer[] = "hey we are starting the project\n";
    write(connection_file_descriptor, write_buffer, strlen(write_buffer));
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

        do_something(connection_file_descriptor);
        close(connection_file_descriptor);
    }

}