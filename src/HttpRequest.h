//
// Created by redonxharja on 5/29/25.
//

#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#include "HttpHeaders.h"
#include "HttpRequestLine.h"
#include "ParseResult.h"
#include "../lib/Dictionary.h"

#define MAX_BODY_LEN (1024 * 1000)
#define MAX_REASON_PHRASE_LEN 64

/* HTTP REQUEST */
typedef struct {
    HttpRequestLine request_line;
    Header headers[MAX_HEADERS];
    size_t header_count;
    char body[MAX_BODY_LEN];
} HttpRequest;

ParseResult parse_request(const char * buf, size_t len, HttpRequest * req);
void show_request(const HttpRequest * req);

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

ReadHeaderResult recv_header(int fd, char *header_buf, ssize_t header_cap);
ParseResult parse_header(const char *buf, size_t len, HttpRequest *req);
ParseStatus parse_content_length(const char * val, size_t * out);

/* CONTENT */
typedef struct {
    char * body;
    long body_len;
} Content;

typedef struct {
    Content content;
    char* Key;
} CachedContent;

typedef struct {
    char version[VERSION_LEN];
    long version_len;
    int port;
} Settings;

//TODO: add data structure to cache requested content
#endif //HTTPREQUEST_H
