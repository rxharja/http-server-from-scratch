//
// Created by redonxharja on 4/27/26.
//

#ifndef HTTPSERVER_HEADER_H
#define HTTPSERVER_HEADER_H
#define RESPONSE_BUFFER_SIZE (128 * 1024 * 1024)
#include <stdio.h>
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

/**
 * Serialize `resp` into the wire format (status line + headers + body) at `buffer`.
 * Adds `Date` and `Connection` headers; respects `resp->head_only` (no body emitted).
 *
 * @param resp         response to serialize
 * @param buffer       output buffer
 * @param buffer_size  capacity of `buffer`
 * @param keep_alive   non-zero to emit `Connection: keep-alive`, zero for `close`
 * @return             bytes written, or negative on overflow / serialization error
 */
ssize_t response_serialize(const HttpResponse * resp, char * buffer, size_t buffer_size, int keep_alive);

#endif //HTTPSERVER_HEADER_H
