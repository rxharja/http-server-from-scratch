//
// Created by redonxharja on 5/4/26.
//

#ifndef HTTPSERVER_CONNECTION_H
#define HTTPSERVER_CONNECTION_H
#include <stdio.h>
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

typedef enum {
    SEND_OK,
    SEND_PEER_CLOSED,
    SEND_HAS_MORE,
    SEND_ERROR
} SendReponseStatus;

typedef struct {
    ReadBodyStatus status;
    size_t         next_req_offset;
} ReadBodyResult;

typedef struct {
    size_t cap;
    size_t size;
    char * buffer;
} HttpBuffer;

typedef struct {
    int    bytes_to_send;
    int    keep_alive;
    size_t next_req_offset;
} KeepAliveStatus;

typedef enum {
    CONN_READING_REQUEST,
    CONN_READING_BODY_CL, // content length
    CONN_READING_BODY_CHUNKED,
    CONN_SENDING_100, // 100-continue
    CONN_BUILDING,
    CONN_SENDING_RESPONSE,
    CONN_CLOSED
} ConnPhase;

typedef struct { size_t received; } CLBodySt;

typedef struct {
    size_t       consumed;   // bytes already fed to dec, relative to body_start
    ChunkDecoder dec;
} ChunkedBodySt;

typedef struct {
    size_t sent;
} SendSt;

typedef struct {
    int fd;
    ConnPhase phase;
    int keep_alive;
    HttpBuffer req_buf;
    HttpBuffer resp_buf;
    HttpBuffer body_dechunked;
    HttpRequest req_parsed;
    size_t body_start;
    size_t body_len;            // for Content-Length
    TransferCoding body_coding; // for Transfer-Encoding: Chunked
    size_t next_req_offset;
    size_t requests;
    union {
        CLBodySt cl;
        ChunkedBodySt chunked;
        SendSt send;
    } st;
} Connection;

ReadHeaderResult conn_recv_header(int fd, HttpBuffer * req);

// invariant: result.body_received is in the set of [0, body_len]
ReadBodyResult conn_recv_body_cl(int fd, HttpBuffer * req, size_t body_start, size_t body_len, CLBodySt * st);

ReadBodyResult conn_recv_body_chunked(int fd, HttpBuffer *req_buf, size_t body_start, HttpBuffer *dechunked, ChunkedBodySt * st);

HttpResponse response_error_405(const char * const *allowed, size_t allowed_count, const HttpBuffer * allow_buf, ResponseHeader *h);

KeepAliveStatus connection_step_process(Connection * conn, const Router * router);

#endif //HTTPSERVER_CONNECTION_H
