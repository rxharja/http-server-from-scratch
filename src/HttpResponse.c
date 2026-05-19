//
// Created by redonxharja on 4/27/26.
//

#include "http_server/HttpResponse.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "parser.h"

static int write_header(char * write_buf, const size_t cap, const ResponseHeader * header) {
    int written = 0;
    written = snprintf(write_buf, cap, "%s: %s\r\n", header->key, header->value);
    if (written < 0 || (size_t)written >= cap) return -1;
    return written;
}

static size_t write_current_time(char * write_buf, const size_t cap) {
    // 1. Get the current calendar time
    const time_t now = time(NULL);
    if (now == (time_t)-1) return 0;

    // 2. Convert to GMT/UTC structure
    const struct tm *gmt_info = gmtime(&now);

    // 3. Format the time: %d (day), %b (abbreviated month), %Y (year), etc.

    return strftime(write_buf, cap, "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", gmt_info);
}

static int write_connection(char * write_buf, const size_t cap, const int keep_alive) {
    int written = 0;
    const char * format = keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    return snprintf(write_buf, cap, format);
}

ssize_t serialize_response(const HttpResponse * resp, char * buffer, const size_t buffer_size, const int keep_alive) {
    ssize_t offset = 0;

    // Start line
    int written = snprintf(buffer + offset, buffer_size - offset,
        "%s %d %s\r\n", "HTTP/1.1", resp->status, resp->reason);

    if (written < 0 || (size_t)written > buffer_size - offset) return -1;
    offset += written;

    // current time in GMT
    const size_t time_written = write_current_time(buffer + offset, buffer_size - offset);
    if (time_written == 0) return -1;
    offset += time_written;

    // Headers
    int saw_content_length = 0;
    for (int i = 0; i < resp->header_count; i++) {
        written = write_header(buffer + offset, buffer_size - offset, &resp->headers[i]);
        if (written < 0 ) return -1;
        offset += written;
        if (ascii_ieq(resp->headers[i].key, "content-length")) saw_content_length = 1;
    }

    if (!saw_content_length && resp->body_len > 0) {
        written = snprintf(buffer + offset, buffer_size - offset,
            "Content-Length: %zu\r\n", resp->body_len);
        if (written < 0 || (size_t)written >= buffer_size - offset) return -1;
        offset += written;
    }

    written = write_connection(buffer + offset, buffer_size - offset, keep_alive);
    if (written < 0 || (size_t)written >= buffer_size - offset) return -1;
    offset += written;

    // Blank line
    if (offset + 2 >= buffer_size) return -1;
    buffer[offset++] = '\r'; buffer[offset++] = '\n';

    // Body
    if (!resp->head_only && resp->body && resp->body_len > 0) {
        if (offset + resp->body_len > buffer_size) return -1;
        memcpy(buffer + offset, resp->body, resp->body_len);
        offset += resp->body_len;
    }

    return offset; // total bytes written
}
