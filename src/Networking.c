//
// Created by redonxharja on 5/24/26.
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include "Networking.h"
#include <fcntl.h>

#include "Connection.h"
#include "parser.h"

#define TIMEOUT 30

static const char *inet_ntop2(void *addr, char *buf, const size_t size) {
    const struct sockaddr_storage *sas = addr;
    struct sockaddr_in *sa4;
    struct sockaddr_in6 *sa6;
    void *src;

    switch (sas->ss_family) {
        case AF_INET:
            sa4 = addr;
            src = &sa4->sin_addr;
            break;
        case AF_INET6:
            sa6 = addr;
            src = &sa6->sin6_addr;
            break;
        default:
            return NULL;
    }

    return inet_ntop(sas->ss_family, src, buf, size);
}

int listener_socket_get(const char * port, const size_t backlog) {
    int listener = -1;     // Listening socket descriptor
    const int yes=1;  // For setsockopt() SO_REUSEADDR, below
    int rv;

    struct addrinfo hints = {0}, *ai, *p;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, port, &hints, &ai)) != 0) {
        fprintf(stderr, "pollserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) continue;

        // Lose the "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;
    }

    // If we got here, it means we didn't get bound
    if (p == NULL) return -1;

    freeaddrinfo(ai); // All done with this

    if (listen(listener, (int)backlog) == -1) return -1;

    return listener;
}

static void connection_init(Connection * conn, const int new_fd) {
    memset(&conn->req_parsed, 0, sizeof(conn->req_parsed));
    memset(&conn->st, 0, sizeof(conn->st));

    conn->fd = new_fd;
    conn->req_buf.buffer = calloc(1, HTTP_MAX_REQUEST_LEN * sizeof(char));
    conn->req_buf.cap = HTTP_MAX_REQUEST_LEN;
    conn->req_buf.size = 0;

    conn->resp_buf.buffer = calloc(1, HTTP_RESPONSE_BUFFER_SIZE * sizeof(char));
    conn->resp_buf.cap = HTTP_RESPONSE_BUFFER_SIZE;
    conn->resp_buf.size = 0;

    conn->body_dechunked.buffer = calloc(1, HTTP_MAX_DECHUNK_SIZE * sizeof(char));
    conn->body_dechunked.cap = HTTP_MAX_DECHUNK_SIZE;
    conn->body_dechunked.size = 0;

    conn->requests = 0;
    conn->phase = CONN_READING_REQUEST;
}

static void add_to_client_set(ClientSet *client_set, const int new_fd) {
    if (client_set == NULL || client_set->poll_fd_set == NULL || client_set->conns == NULL) return;

    // If we don't have room, add more space in the poll fd array
    if (client_set->fd_count == client_set->fd_size) {
        client_set->fd_size *= 2; // double it
        client_set->poll_fd_set = realloc(client_set->poll_fd_set, sizeof(*client_set->poll_fd_set) * client_set->fd_size);
        client_set->conns = realloc(client_set->conns, sizeof(*client_set->conns) * client_set->fd_size);
    }

    client_set->poll_fd_set[client_set->fd_count].fd = new_fd;
    client_set->poll_fd_set[client_set->fd_count].events = POLLIN;
    client_set->poll_fd_set[client_set->fd_count].revents = 0;
    connection_init(&client_set->conns[client_set->fd_count], new_fd);

    client_set->fd_count++;
}

void client_set_delete(ClientSet *client_set, const int i) {
    // Copy the one from the end over this one
    client_set->poll_fd_set[i] = client_set->poll_fd_set[client_set->fd_count-1];
    client_set->conns[i] = client_set->conns[client_set->fd_count-1];
    client_set->fd_count--;
}


void client_set_add_new(const int listener, ClientSet *client_set) {
    // const struct timeval timeout = { .tv_sec = TIMEOUT, .tv_usec = 0 };
    struct sockaddr_storage remote_addr; // Client address
    socklen_t addrlen;

    // Newly accept()ed socket descriptor
    char remoteIP[INET6_ADDRSTRLEN];

    addrlen = sizeof remote_addr;
    const int new_fd = accept(listener, (struct sockaddr *) &remote_addr, &addrlen);
    // setsockopt(new_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (new_fd == -1) {
        perror("accept");
        return;
    }
    const int flags = fcntl(new_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(new_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl");
        close(new_fd);          // don't leak the fd or add a blocking socket to the set
        return;
    }

    add_to_client_set(client_set, new_fd);
    printf("HTTP Server: new connection from %s on socket %d\n",
            inet_ntop2(&remote_addr, remoteIP, sizeof remoteIP),
            new_fd);
}