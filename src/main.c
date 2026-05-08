#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include "Connection.h"
#include "HttpRequest.h"
#include "../lib/Dictionary.h"

#define BACKLOG 10

void sigchild_handler(int s) {
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0) {}

    errno = saved_errno;
}

static volatile sig_atomic_t keepRunning = 1;

void intHandler(const int sig) {
    (void)sig; // unused
    keepRunning = 0;
}


int main(int argc, char *argv[]) {
    signal(SIGINT, intHandler);

    int sockfd;
    struct addrinfo *servinfo = 0;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    struct sigaction sa;
    char s[INET6_ADDRSTRLEN];

    if (argc != 2) {
       printf("Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (valid_port(argv[1]) != 0) {
        printf("Invalid port number\n");
        exit(EXIT_FAILURE);
    }

    if (get_addr_info(&servinfo, argv[1]) != EXIT_SUCCESS) exit(EXIT_FAILURE);
    if ((sockfd = bind_socket(servinfo)) < 3) exit(EXIT_FAILURE);

    freeaddrinfo(servinfo);

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    sa.sa_handler = sigchild_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    printf("server: Listening on port %s...\n", argv[1]);

    // Dictionary * content_cache = preload_cache();

    while (keepRunning) {
        sin_size = sizeof their_addr;

        const int new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);

        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);

        const pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            continue;
        }

        if (pid == 0) {
            close(sockfd);

            handle_connection(new_fd);
            const char *body = "Hello, world!\n";
            char response[256];
            const int n = snprintf(response, sizeof response,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n"
                "\r\n"
                "%s",
                strlen(body), body);

            if (send(new_fd, response, n, 0) == -1) perror("send");
            // show_request(request);

            // HttpResponse *response = pack_response(request, content_cache);
            // free(request);

            // const int res_len = serialize_response(response, sendbuf, sizeof(sendbuf) );

            // if (send(new_fd, sendbuf, res_len, 0) == -1) {
            //     perror("send");
            // }

            // free_response(response);

            // memset(sendbuf, 0, sizeof(sendbuf));

            close(new_fd);
            exit(0);
        }
        close(new_fd);
    }

    // free_dict(content_cache);

    return EXIT_SUCCESS;
}
