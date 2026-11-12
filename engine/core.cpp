#include <sys/socket.h> // socket, bind, listen, accept, connect, AF_UNIX
#include <sys/un.h>     // struct sockaddr_un
#include <unistd.h>     // close, unlink, read, write
#include <string.h>     // memset, strncpy
#include <errno.h>      // errno
#include <stdio.h>      // printf, perror

#define SOCKET_PATH "/tmp/bluec.socket"

// Function to create a Unix domain socket and bind it to a specified path
int create_unix_socket(const char *socket_path) {
    // Create a socket
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Create a socket address
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // Bind the socket to the address
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    // Listen for incoming connections
    if (listen(sockfd, 1) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int clean_up_socket(const char *socket_path) {
    // Unlink the socket file to clean up
    if (unlink(socket_path) < 0) {
        if (errno != ENOENT) {
            perror("unlink");
            return -1;
        }
    }
    return 0;
}

int accept_connection(int sockfd) {
    // Accept an incoming connection
    int client_fd = accept(sockfd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        return -1;
    }
    return client_fd;
}

int connect_to_socket(const char *socket_path) {
    // Create a socket
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Create a socket address
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // Connect to the server socket
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    return sockfd;
}
//TODO: remember this needs to be added in once we figure out where we are going to send said data.
int send_data(int sockfd, const char *data, size_t length) {
    ssize_t bytes_sent = write(sockfd, data, length);
    if (bytes_sent < 0) {
        perror("write");
        return -1;
    }
    return bytes_sent;
}

// Function to receive data from a socket
int receive_data(int sockfd, char *buffer, size_t buffer_size) {
    ssize_t bytes_received = read(sockfd, buffer, buffer_size);
    if (bytes_received < 0) {
        perror("read");
        return -1;
    }
    return bytes_received;
}

int close_socket(int sockfd) {
    if (close(sockfd) < 0) {
        perror("close");
        return -1;
    }
    return 0;
}

int main() {
    const char *socket_path = "/tmp/blue_socket";

    // Remove a stale socket left by a previous server instance.
    clean_up_socket(socket_path);

    // Create a Unix domain socket
    int server_sockfd = create_unix_socket(socket_path);
    if (server_sockfd < 0) {
        return 1;
    }

    printf("Server is listening on %s\n", socket_path);

    bool exit_requested = false;
    while (!exit_requested) {
        int client_sockfd = accept_connection(server_sockfd);
        if (client_sockfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        printf("Client connected\n");
        char buffer[256];
        for (;;) {
            // Reserve one byte for the null terminator.
            int bytes_received = receive_data(client_sockfd, buffer,
                                              sizeof(buffer) - 1);
            if (bytes_received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (bytes_received == 0) {
                break;
            }

            buffer[static_cast<size_t>(bytes_received)] = '\0';
            printf("Received: %s\n", buffer);
            
            // A client can shut down the server by sending "exit".
            if (strncmp(buffer, "exit", 4) == 0) {
                exit_requested = true;
                break;
            }
        }

        close_socket(client_sockfd);
    }
 // Clean up and exit
    close_socket(server_sockfd);
    clean_up_socket(socket_path);

    return 0;
}
