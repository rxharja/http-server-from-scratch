#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include "HttpRequest.h"

#define BACKLOG 10

int get_addr_info(struct addrinfo **serv_info, const char * port) {
    struct addrinfo hints = {0};
    int rv;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, port, &hints, serv_info)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int bind_socket(const struct addrinfo * servinfo) {
    const int yes = 1;
    int sockfd = 0;
    const struct addrinfo * p;

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            return EXIT_FAILURE;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        return EXIT_FAILURE;
    }

    return sockfd;
}

void sigchild_handler(int s) {
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0) {}

    errno = saved_errno;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &((struct sockaddr_in*)sa)->sin_addr;
    }

    return &((struct sockaddr_in6*)sa)->sin6_addr;
}

int valid_port(const char * str) {
    char *endptr;
    errno = 0;

    long num = strtol(str, &endptr, 10);

    if (errno != 0 || *endptr != '\0' || endptr == str) return 1;
    if (num <= 0 || num > 65535) return 1;
    return 0;
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct addrinfo *servinfo = 0;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    struct sigaction sa;
    char s[INET6_ADDRSTRLEN];
    char recvbuf[1024];
    char sendbuf[100000];

    if (valid_port(argv[1]) != 0) {
        printf("Invalid port number\n");
        exit(EXIT_FAILURE);
    }

    if (get_addr_info(&servinfo, argv[1]) != EXIT_SUCCESS) {
        exit(EXIT_FAILURE);
    }

    if ((sockfd = bind_socket(servinfo)) < 3) {
        exit(EXIT_FAILURE);
    }

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

    while (1) {
        sin_size = sizeof their_addr;

        const int new_fd = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);

        if (new_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        // printf("origin: %s\n", s);

        if (!fork()) {
            close(sockfd);

            recv(new_fd, &recvbuf, sizeof(recvbuf), 0);

            HttpRequest *request = malloc(sizeof(HttpRequest));
            parse_request(recvbuf, request);
            show_request(request);
            memset(recvbuf, 0, sizeof(recvbuf));

            HttpResponse *response = pack_response(request);

            free(request);

            int res_len = serialize_response(response, sendbuf, sizeof(sendbuf) );

            if (send(new_fd, sendbuf, res_len, 0) == -1) {
                perror("send");
            }

            free_response(response);

            memset(sendbuf, 0, sizeof(sendbuf));

            close(new_fd);
            exit(0);
        }

        close(new_fd);
    }

    return EXIT_SUCCESS;
}
