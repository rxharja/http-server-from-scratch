//
// Created by redonxharja on 5/29/25.
//

#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#define MAX_HEADERS 32
#define MAX_METHOD_LEN 8
#define MAX_PATH_LEN 256
#define MAX_VERSION_LEN 16
#define MAX_HEADER_KEY_LEN 64
#define MAX_HEADER_VALUE_LEN 256
#define MAX_REASON_PHRASE_LEN 64
#include "lib/Dictionary.h"

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

typedef enum {
    PARSE_OK, PARSE_BAD_REQUEST, PARSE_URI_TOO_LONG, PARSE_HEADER_TOO_LONG
  } ParseHeaderStatus;

typedef struct {
    ParseHeaderStatus status;
    ssize_t error_position;
} ParseHeaderResult;

typedef enum {
    GET, POST, PUT, DELETE, OPTIONS, HEAD, PATCH, UNKNOWN
} HttpMethod;

typedef struct {
    char key[MAX_HEADER_KEY_LEN];
    char value[MAX_HEADER_VALUE_LEN];
} Header;

typedef struct {
    char * body;
    long body_len;
} Content;

typedef struct {
    Content content;
    char* Key;
} CachedContent;

typedef struct {
    HttpMethod method;
    char path[MAX_PATH_LEN];
    char version[MAX_VERSION_LEN];
    Header headers[MAX_HEADERS];
    int header_count;
} HttpRequest;

ParseHeaderResult parse_header(const char *buf, size_t len, HttpRequest *req);

typedef struct {
    char version[MAX_VERSION_LEN];
    long version_len;
    int port;
} Settings;

HttpMethod parse_http_method(const char *method);

char* show_http_method(HttpMethod method);

void parse_request(char * message, HttpRequest * req);

void show_request(HttpRequest * req);

//TODO: add data structure to cache requested content
#endif //HTTPREQUEST_H
