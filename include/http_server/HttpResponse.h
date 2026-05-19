//
// Created by redonxharja on 4/27/26.
//

#ifndef HTTPSERVER_HEADER_H
#define HTTPSERVER_HEADER_H
#define RESPONSE_BUFFER_SIZE (128 * 1024 * 1024)
#include <sys/types.h>
#include "HttpRequest.h"

typedef struct {
    const char *key;
    const char *value;
} ResponseHeader;

typedef struct {
    int status;                    // 200, 404, 500
    const char *reason;            // "OK", "Not Found", etc.
    const ResponseHeader *headers; // caller-owned, usually static
    size_t header_count;
    const char *body;             // body bytes, may be NULL
    size_t body_len;
    int head_only;
} HttpResponse;

HttpResponse from_static(const HttpRequest * req, const char * buf, size_t len);

ssize_t serialize_response(const HttpResponse * resp, char * buffer, size_t buffer_size, int keep_alive);

#endif //HTTPSERVER_HEADER_H
