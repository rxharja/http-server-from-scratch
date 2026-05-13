//
// Created by redonxharja on 5/4/26.
//

#ifndef HTTPSERVER_CONNECTION_H
#define HTTPSERVER_CONNECTION_H
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include "HttpRequest.h"
#include "HttpResponse.h"

/* READ HEADER */
typedef enum {
    READ_HEADER_OK = 0,
    READ_HEADER_PEER_CLOSED,
    READ_HEADER_TOO_LARGE,
    READ_HEADER_IO_ERROR,
} ReadHeaderStatus;

typedef struct {
    ReadHeaderStatus status;
    ssize_t          total_received;
    ssize_t          body_start;   // offset just past \r\n\r\n
} ReadHeaderResult;

typedef enum {
    READ_BODY_OK = 0,
    READ_BODY_PEER_CLOSED,
    READ_BODY_TOO_LARGE,
    READ_BODY_OVERREAD,
    READ_BODY_IO_ERROR,
    READ_BODY_BAD_DATA,
} ReadBodyStatus;

typedef struct {
    ReadBodyStatus status;
    size_t          body_received;
    size_t          next_req_offset;
} ReadBodyResult;

typedef struct {
    size_t cap;
    size_t size;
    char * buffer;
} HttpBuffer;

typedef struct {
    HttpBuffer http_buffer;
    size_t already_have;
} ReadBuffer;

typedef struct {
    int bytes_to_send;
    int keep_alive;
    size_t next_req_offset;
} KeepAliveStatus;

int get_addr_info(struct addrinfo **serv_info, const char * port);

int bind_socket(const struct addrinfo * servinfo);

void *get_in_addr(struct sockaddr *sa);

int valid_port(const char * str);

ReadHeaderResult recv_header(int fd, char *header_buf, size_t already_have, ssize_t header_cap);

ReadBodyResult recv_chunked_body(int fd, char *buf, size_t have, size_t buf_cap, char * dest_buf);

/// invariant: result.body_received is in the set of [0, body_len]
ReadBodyResult recv_body(int fd, const char *buf, size_t already_have, size_t body_len, char * dest_buf);

HttpResponse synthesize_405(const char * const *allowed, size_t allowed_count, const HttpBuffer * allow_buf, ResponseHeader *h);

KeepAliveStatus handle_connection(int fd, const Route routes[], size_t route_count, HttpBuffer *res_buffer, ReadBuffer *req_buffer);

#endif //HTTPSERVER_CONNECTION_H