//
// Created by redonxharja on 4/27/26.
//

#include "HttpResponse.h"
#include <string.h>

ssize_t serialize_response(const HttpResponse * resp, char * buffer, const size_t buffer_size) {
    ssize_t offset = 0;

    // Start line
    int written = snprintf(buffer + offset, buffer_size - offset,
        "%s %d %s\r\n", "HTTP/1.1", resp->status, resp->reason);

    if (written < 0 || written > buffer_size - offset) return -1;

    offset += written;

    // Headers
    for (int i = 0; i < resp->header_count; i++) {
        written = snprintf(buffer + offset, buffer_size - offset,
                           "%s: %s\r\n", resp->headers[i].key, resp->headers[i].value);
        if (written < 0 || written > buffer_size - offset) return -1;
        offset += written;
    }

    // Blank line
    if (offset + 2 >= buffer_size) return -1;
    buffer[offset++] = '\r'; buffer[offset++] = '\n';

    // Body
    if (resp->body && resp->body_len > 0) {
        if (offset + resp->body_len > buffer_size) return -1;
        memcpy(buffer + offset, resp->body, resp->body_len);
        offset += resp->body_len;
    }

    return offset; // total bytes written
}

const Route * route_lookup(const Route routes[], const size_t count, const char * method, const char * path) {
    for (int i = 0; i < count; i++) {
        const Route * route = &routes[i];
        if (strcmp(route->method, method) == 0 && strcmp(route->path, path) == 0) {
            return route;
        }
    }

    return NULL;
}