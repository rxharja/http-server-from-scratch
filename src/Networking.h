//
// Created by redonxharja on 5/24/26.
//

#ifndef HTTPSERVER_NETWORKING_H
#define HTTPSERVER_NETWORKING_H

#include <stdint.h>
#include "Connection.h"

// struct for managing connections and poll_fds in one go
typedef struct {
    struct pollfd poll_fd_set[HTTP_MAX_CONNECTIONS + 1]; // used for polling fd's, [0] = listener, [1..MAX] = clients
    Connection          conns[HTTP_MAX_CONNECTIONS + 1]; // parallel array to poll_fd_set tracking connection state
    uint8_t     arena_backing[HTTP_MAX_CONNECTIONS][HTTP_CONN_ARENA_SIZE];
}ClientSet;

//Return a listening socket.
int listener_socket_get(const char * port, size_t backlog);

// Remove a file descriptor at a given index from the set.
void client_set_delete(ClientSet *client_set, int i);

// Handle incoming connections.
void client_set_add_new(int listener, ClientSet *client_set);

void connection_close(ClientSet * client_set, int i);

#endif //HTTPSERVER_NETWORKING_H
