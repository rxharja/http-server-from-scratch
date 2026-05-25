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
    READ_HEADER_HAS_MORE
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
    READ_BODY_IO_ERROR,
    READ_BODY_BAD_DATA,
    READ_BODY_HAS_MORE
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
    HttpBuffer body_dechunked;
    size_t body_len; // set after content-length is parsed
    size_t body_start;
    size_t body_received;
    HttpRequest req_parsed; // populated once header is done
    HttpBuffer resp; // buffer + total size + how much sent
    size_t sent; // send offset
    int keep_alive;
} Connection;

ReadHeaderResult recv_header(Connection *conn);

ReadBodyResult recv_chunked_body(Connection * conn);

// invariant: result.body_received is in the set of [0, body_len]
ReadBodyResult recv_body(Connection * conn, size_t body_len);

HttpResponse synthesize_405(const char * const *allowed, size_t allowed_count, const HttpBuffer * allow_buf, ResponseHeader *h);

KeepAliveStatus handle_connection(Connection * conn, const Router * router);

#endif //HTTPSERVER_CONNECTION_H