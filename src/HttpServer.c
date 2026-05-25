//
// Created by RedonXharja on 5/8/2026.
//

#include "http_server/HttpServer.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/poll.h>
#include "Connection.h"
#include "Networking.h"

static void cleanup_fd(ClientSet *client_set, int *pfd_i) {
    close(client_set->poll_fd_set[*pfd_i].fd);
    del_from_client_set(client_set, *pfd_i);
    (*pfd_i)--; // re-examine the slot we just deleted
}

static int has_duplicate_routes(const Router * router) {
    const char * get = show_http_method(GET);

    for (int i = 0; i < router->route_count; i++) {
        const Route * route = &router->routes[i];
        if (route->method != get) continue;
        const CachedFile * file = dict_find(router->static_cache, route->path);
        if (file) return 1;
    }

    for (int i = 0; i < router->route_count - 1; i++) {
        const Route * route1 = &router->routes[i];
        for (int j = 1; j < router->route_count; j++) {
            const Route * route2 = &router->routes[j];
            if (strcmp(route1->path, route2->path) == 0 && strcmp(route1->method, route2->method) == 0) return 1;
        }
    }

    return 0;
}

static void handle_client_data(ClientSet * client_set, int *pfd_i, const Router * router) {
    client_set->conns[*pfd_i].req.http_buffer.buffer = malloc(MAX_REQUEST_LEN * sizeof(char));
    client_set->conns[*pfd_i].req.http_buffer.cap = MAX_REQUEST_LEN;

    client_set->conns[*pfd_i].resp.buffer = malloc(RESPONSE_BUFFER_SIZE * sizeof(char));
    client_set->conns[*pfd_i].resp.cap = RESPONSE_BUFFER_SIZE;

    ReadBuffer * request_buffer = &client_set->conns[*pfd_i].req;
    const HttpBuffer * response_buffer = &client_set->conns[*pfd_i].resp;

    size_t requests = 0;
    while (requests <= MAX_REQUESTS) {
        const KeepAliveStatus status = handle_connection(&client_set->conns[*pfd_i], router);

        if (status.bytes_to_send <= 0) {
            if (status.bytes_to_send < 0) perror("handle_connection");
            cleanup_fd(client_set, pfd_i);
            return;
        }

        const ssize_t bytes_sent = send(client_set->poll_fd_set[*pfd_i].fd, response_buffer->buffer, status.bytes_to_send, 0);
        if (bytes_sent != status.bytes_to_send) perror("send");

        requests++;

        if (!status.keep_alive) break;
        if (status.next_req_offset <= 0) continue;

        // should be replaced by a ring-buffer for performance.
        // can be done when we replace malloc calling every time with static or upfront allocation
        const size_t bytes_to_move = request_buffer->http_buffer.size - status.next_req_offset;
        memmove(request_buffer->http_buffer.buffer,
            request_buffer->http_buffer.buffer + status.next_req_offset,
              bytes_to_move);

        request_buffer->already_have = bytes_to_move;
    }

    cleanup_fd(client_set, pfd_i);
    free(request_buffer->http_buffer.buffer);
    free(response_buffer->buffer);
}

int run_server(const char * port, const Router * router, const size_t backlog) {
    if (has_duplicate_routes(router)) {
        perror("Cannot have duplicate routes.\n");
        exit(EXIT_FAILURE);
    }

    const int listener = get_listener_socket(port, backlog);
    if (listener == -1) exit(EXIT_FAILURE);
    printf("server: Listening on port %s...\n", port);

    // temporarily manually allocating, but for an embedded deploy we should allocate everything upfront
    ClientSet client_set = {0};
    client_set.fd_size = 8;
    client_set.poll_fd_set = malloc(sizeof(*client_set.poll_fd_set) * client_set.fd_size);
    client_set.conns = malloc(sizeof(*client_set.conns) * client_set.fd_size);
    client_set.poll_fd_set[0].fd = listener;
    client_set.poll_fd_set[0].events = POLLIN;
    client_set.poll_fd_set[0].revents = 0;
    client_set.fd_count = 1;

    for (;;) {
        const int poll_count = poll(client_set.poll_fd_set, client_set.fd_size, -1);

        if (poll_count == -1) {
            perror("poll");
            free(client_set.poll_fd_set);
            free(client_set.conns);
            exit(1);
        }

        // Run through connections looking for data to read
        for(int i = 0; i < client_set.fd_count; i++) {
            if (client_set.poll_fd_set[i].revents & (POLLIN | POLLHUP)) {
                if (client_set.poll_fd_set[i].fd == listener) add_new_client(listener, &client_set);
                else handle_client_data(&client_set, &i, router);
            }
        }
    }
}

