//
// Created by redonxharja on 5/24/26.
//

#ifndef HTTPSERVER_NETWORKING_H
#define HTTPSERVER_NETWORKING_H
#include "Connection.h"

// struct for managing connections and poll_fds in one go
typedef struct {
    int fd_size; // capacity used for both conns and poll_fd_set
    int fd_count; // how many within capacity, for both conns and poll_fd_set
    struct pollfd *poll_fd_set; // used for polling fd's
    Connection *conns; // parallel array to poll_fd_set tracking connection state
}ClientSet;

//Return a listening socket.
int listener_socket_get(const char * port, size_t backlog);

// Remove a file descriptor at a given index from the set.
void client_set_delete(ClientSet *client_set, int i);

// Handle incoming connections.
void client_set_add_new(int listener, ClientSet *client_set);

#endif //HTTPSERVER_NETWORKING_H
