//
// Created by RedonXharja on 5/8/2026.
//

#include "http_server/HttpServer.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/poll.h>
#include <errno.h>
#include <assert.h>
#include "Connection.h"
#include "Networking.h"

int server_port_valid(const char * str) {
    char *end_ptr;
    errno = 0;

    const long num = strtol(str, &end_ptr, 10);

    if (errno != 0 || *end_ptr != '\0' || end_ptr == str) return 1;
    if (num <= 0 || num > 65535) return 1;
    return 0;
}

static short conn_phase_event(const ConnPhase phase) {
    assert(phase != CONN_BUILDING); // this work is CPU-bound, dispatch loop should not need to worry about this

    switch (phase) {
        case CONN_READING_REQUEST:
        case CONN_READING_BODY_CL:
        case CONN_READING_BODY_CHUNKED:    return POLLIN;

        case CONN_SENDING_RESPONSE:
        case CONN_SENDING_RESPONSE_STREAM: return POLLOUT;

        case CONN_CLOSED:                  return 0; // about to remove from set
        default:                           assert(0); return 0;
    }

    return 0;
}

int server_run(const char * port, const Router * router, const size_t backlog) {
    if (server_port_valid(port) != 0) {
        printf("Invalid port number\n");
        exit(EXIT_FAILURE);
    }

    if (router_has_duplicate_routes(router)) {
        perror("Cannot have duplicate routes.\n");
        exit(EXIT_FAILURE);
    }

    const int listener = listener_socket_get(port, backlog);
    if (listener == -1) exit(EXIT_FAILURE);
    printf("server: Listening on port %s...\n", port);

    static ClientSet client_set;
    client_set.poll_fd_set[0].fd = listener;
    client_set.poll_fd_set[0].events = POLLIN;
    client_set.poll_fd_set[0].revents = 0;

    // mark all clients as free with -1
    for (int i = 1; i <= HTTP_MAX_CONNECTIONS; i++) {
       client_set.poll_fd_set[i].fd = -1;
    }

    for (;;) {
        const int poll_count = poll(client_set.poll_fd_set, HTTP_MAX_CONNECTIONS + 1, -1);

        if (poll_count == -1) {
            perror("poll");
            exit(1);
        }

        if (client_set.poll_fd_set[0].revents & POLLIN) client_set_add_new(listener, &client_set);

        // Run through connections after 0 since that is the listener
        for(int i = 1; i <= HTTP_MAX_CONNECTIONS; i++) {
            struct pollfd * poll_fd = &client_set.poll_fd_set[i];
            if (poll_fd->fd < 0 ) continue;
            if (!(poll_fd->revents & (POLLIN | POLLHUP | POLLOUT))) continue;
            Connection * conn = &client_set.conns[i];
            connection_step_process(conn, router);
            poll_fd->events = conn_phase_event(conn->phase);
            if (conn->phase == CONN_CLOSED) connection_close(&client_set, i);
        }
    }
}

