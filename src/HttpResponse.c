//
// Created by redonxharja on 4/27/26.
//

#include "HttpResponse.h"
#include <string.h>

int serialize_response(const HttpResponse * resp, char * buffer, size_t buffer_size) {
    int offset = 0;

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
    buffer[offset++] = '\r';
    buffer[offset++] = '\n';

    // Body
    if (resp->body && resp->body_len > 0) {
        if (offset + resp->body_len > buffer_size) return -1;
        memcpy(buffer + offset, resp->body, resp->body_len);
        offset += resp->body_len;
    }

    return offset; // total bytes written
}