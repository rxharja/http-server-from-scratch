//
// Created by redonxharja on 5/4/26.
//

#ifndef HTTPSERVER_CONNECTION_H
#define HTTPSERVER_CONNECTION_H
#include <stdio.h>
#include "http_server/Arena.h"
#include "http_server/HttpBuffer.h"
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
    CONN_SENDING_RESPONSE_STREAM,
    CONN_CLOSED
} ConnPhase;

typedef struct {
    Arena arena;               // bump allocator over this slot's backing
    size_t req_mark;           // checkpoint; arena_reset_to() here between keep-alive requests
    HttpBuffer req_buf;       // raw request bytes
    HttpBuffer resp_buf;       // response staging
    HttpBuffer body_dechunked; // decoded chunked body
} ConnMem;

typedef struct {
    int fd;
    ConnPhase phase;
    int keep_alive;
    ConnMem mem;
    HttpRequest req_parsed;
    size_t body_start;
    size_t body_len;            // for Content-Length
    TransferCoding body_coding; // for Transfer-Encoding: Chunked
    Stream producer;
    size_t next_req_offset;
    size_t requests;
    union {
        CLBodySt cl;
        ChunkedBodySt chunked;
        SendSt send;
        SendStreamSt stream;
    } st;
} Connection;

ReadHeaderResult conn_recv_header(int fd, HttpBuffer * req);

void connection_step_process(Connection * conn, const Router * router);

#endif //HTTPSERVER_CONNECTION_H
