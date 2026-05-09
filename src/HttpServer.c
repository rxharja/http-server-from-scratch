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

    while (1) {
        sin_size = sizeof their_addr;

        const int new_fd = accept(sock_fd, (struct sockaddr *) &their_addr, &sin_size);

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
            char buf[RESPONSE_BUFFER_SIZE] = {0};
            const HttpResponse res = handle_connection(new_fd, routes, count);
            const int response_size = serialize_response(&res, buf, RESPONSE_BUFFER_SIZE);

            if (response_size <= 0) {
                perror("serialize_response");
                return response_size;
            }
            printf("response: %s\n", buf);
            send(new_fd, buf, response_size, 0);
            close(new_fd);
            exit(0);
        }
        close(new_fd);
    }
}

