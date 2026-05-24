//
// Created by redonxharja on 5/24/26.
//

#ifndef HTTPSERVER_NETWORKING_H
#define HTTPSERVER_NETWORKING_H
#include "Connection.h"

// network to presentation for ipv4 and ipv6
const char *inet_ntop2(void *addr, char *buf, size_t size);

void *get_in_addr(struct sockaddr *sa);

//Return a listening socket.
int get_listener_socket(const char * port, size_t backlog);

// Add a new file descriptor to the set.
void add_to_client_set(ClientSet * client_set, int new_fd);

// Remove a file descriptor at a given index from the set.
void del_from_poll_fd_set(struct pollfd poll_fd_set[], int i, int *fd_count);

// Handle incoming connections.
void add_new_client(int listener, ClientSet *client_set);

#endif //HTTPSERVER_NETWORKING_H
