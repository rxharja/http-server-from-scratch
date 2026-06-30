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
    char *endptr;
    errno = 0;

    const long num = strtol(str, &endptr, 10);

    if (errno != 0 || *endptr != '\0' || endptr == str) return 1;
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

static void connection_close(ClientSet * client_set, int *pfd_i) {
    const Connection * conn = &client_set->conns[*pfd_i];
    free(conn->req_buf.buffer);
    free(conn->resp_buf.buffer);
    free(conn->body_dechunked.buffer);
    close(client_set->poll_fd_set[*pfd_i].fd);
    client_set_delete(client_set, *pfd_i);
    (*pfd_i)--; // re-examine the slot we just deleted
}

int server_run(const char * port, const Router * router, const size_t backlog) {
    if (router_has_duplicate_routes(router)) {
        perror("Cannot have duplicate routes.\n");
        exit(EXIT_FAILURE);
    }

    const int listener = listener_socket_get(port, backlog);
    if (listener == -1) exit(EXIT_FAILURE);
    printf("server: Listening on port %s...\n", port);

    // temporarily manually allocating, but for an embedded deploy we should allocate everything upfront
    ClientSet client_set = {0};
    client_set.fd_size = 8;
    client_set.poll_fd_set = calloc(1, sizeof(*client_set.poll_fd_set) * client_set.fd_size);
    client_set.conns = calloc(1, sizeof(*client_set.conns) * client_set.fd_size);
    client_set.poll_fd_set[0].fd = listener;
    client_set.poll_fd_set[0].events = POLLIN;
    client_set.poll_fd_set[0].revents = 0;
    client_set.fd_count = 1;

    for (;;) {
        const int poll_count = poll(client_set.poll_fd_set, client_set.fd_count, -1);

        if (poll_count == -1) {
            perror("poll");
            free(client_set.poll_fd_set);
            free(client_set.conns);
            exit(1);
        }

        if (client_set.poll_fd_set[0].revents & POLLIN) client_set_add_new(listener, &client_set);

        // Run through connections after 0 since that is the listener
        for(int i = 1; i < client_set.fd_count; i++) {
            struct pollfd * poll_fd = &client_set.poll_fd_set[i];
            if (!(poll_fd->revents & (POLLIN | POLLHUP | POLLOUT))) continue;
            Connection * conn = &client_set.conns[i];
            connection_step_process(conn, router);
            poll_fd->events = conn_phase_event(conn->phase);
            if (conn->phase == CONN_CLOSED) connection_close(&client_set, &i);
        }
    }
}

