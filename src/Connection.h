//
// Created by redonxharja on 5/4/26.
//

#ifndef HTTPSERVER_CONNECTION_H
#define HTTPSERVER_CONNECTION_H
#include <stdio.h>
#include <netdb.h>
#include <sys/socket.h>
#include "http_server/HttpResponse.h"
#include "http_server/HttpRouter.h"

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

typedef enum {
    CONN_READING_HEADER,
    CONN_READING_BODY_CL, // content length
    CONN_READING_BODY_CHUNKED,
    CONN_SENDING_RESPONSE
} ConnPhase;

typedef struct {
    int fd;
    ConnPhase phase;
    ReadBuffer req; // buffer + how much is filled
    HttpBuffer resp; // buffer + total size + how much sent
    size_t sent; // send offset
    HttpRequest req_parsed; // populated once header is done
    size_t body_len; // populated once known
    int keep_alive;
} Connection;

// struct for managing connections and poll_fds in one go
typedef struct {
    int fd_size; // capacity used for both conns and poll_fd_set
    int fd_count; // how many within capacity, for both conns and poll_fd_set
    struct pollfd *poll_fd_set; // used for polling fd's
    Connection *conns; // parallel array to poll_fd_set tracking connection state
}ClientSet;

int get_addr_info(struct addrinfo **serv_info, const char * port);

int bind_socket(const struct addrinfo * servinfo);

ReadHeaderResult recv_header(int fd, char *header_buf, size_t already_have, ssize_t header_cap);

ReadBodyResult recv_chunked_body(int fd, char *buf, size_t have, size_t buf_cap, char * dest_buf);

/// invariant: result.body_received is in the set of [0, body_len]
ReadBodyResult recv_body(int fd, const char *buf, size_t already_have, size_t body_len, char * dest_buf);

HttpResponse handle_dynamic_file(const HttpRequest * req, const char * path);

HttpResponse synthesize_405(const char * const *allowed, size_t allowed_count, const HttpBuffer * allow_buf, ResponseHeader *h);

KeepAliveStatus handle_connection(int fd, const Router * router, HttpBuffer *res_buffer, ReadBuffer *req_buffer);

#endif //HTTPSERVER_CONNECTION_H