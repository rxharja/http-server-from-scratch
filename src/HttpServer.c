//
// Created by RedonXharja on 5/8/2026.
//

#include "HttpServer.h"

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "Connection.h"

int run_server(const char * port, const Route routes[], const size_t count, const size_t backlog) {
    int sock_fd;
    struct addrinfo *serv_info = 0;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    char s[INET6_ADDRSTRLEN];

    if (get_addr_info(&serv_info, port) != EXIT_SUCCESS) exit(EXIT_FAILURE);
    if ((sock_fd = bind_socket(serv_info)) < 3) exit(EXIT_FAILURE);

    freeaddrinfo(serv_info);

    if (listen(sock_fd, backlog) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("server: Listening on port %s...\n", port);

    const struct timeval timeout = { .tv_sec = 30, .tv_usec = 0 };

    while (1) {
        sin_size = sizeof their_addr;

        const int new_fd = accept(sock_fd, (struct sockaddr *) &their_addr, &sin_size);
        setsockopt(new_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);

        const pid_t pid = fork();
        // const pid_t pid = 0; // for debugging on main process

        if (pid < 0) {
            perror("fork");
            continue;
        }

        if (pid == 0) {
            close(sock_fd);
            ReadBuffer request_buffer = {0};
            HttpBuffer response_buffer = {0};

            request_buffer.http_buffer.buffer = malloc(MAX_QUERY_LEN * sizeof(char));
            request_buffer.http_buffer.cap = MAX_QUERY_LEN;

            response_buffer.buffer = malloc(RESPONSE_BUFFER_SIZE * sizeof(char));
            response_buffer.cap = RESPONSE_BUFFER_SIZE;

            size_t requests = 0;
            while (requests <= MAX_REQUESTS) {
                const KeepAliveStatus status = handle_connection(new_fd, routes, count, &response_buffer, &request_buffer);

                if (status.bytes_to_send <= 0) {
                    perror("handle_connection");
                    close(new_fd);
                    exit(1);
                }

                send(new_fd, response_buffer.buffer, status.bytes_to_send, 0);

                requests++;

                if (!status.keep_alive) break;
                if (status.next_req_offset <= 0) continue;

                const size_t bytes_to_move = request_buffer.http_buffer.size - status.next_req_offset;

                memmove(request_buffer.http_buffer.buffer,
                    request_buffer.http_buffer.buffer + status.next_req_offset,
                      bytes_to_move);

                request_buffer.already_have = bytes_to_move;
            }

            close(new_fd);
            free(request_buffer.http_buffer.buffer); free(response_buffer.buffer);
            exit(0);
        }
        close(new_fd);
    }
}

ContentCache * content_cache_create() {
    return dict_init();
}

int cache_file(ContentCache * cache, const char * url_path, const CachedFile * file) {
    return -1;
}