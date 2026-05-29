//
// Created by redonxharja on 4/27/26.
//

#include "http_server/HttpResponse.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <asm-generic/errno-base.h>
#include <sys/socket.h>
#include "parser.h"

static int response_header_write(char * write_buf, const size_t cap, const ResponseHeader * header) {
    int written = 0;
    written = snprintf(write_buf, cap, "%s: %s\r\n", header->key, header->value);
    if (written < 0 || (size_t)written >= cap) return -1;
    return written;
}

static size_t response_current_time_write(char * write_buf, const size_t cap) {
    // 1. Get the current calendar time
    const time_t now = time(NULL);
    if (now == (time_t)-1) return 0;

    // 2. Convert to GMT/UTC structure
    const struct tm *gmt_info = gmtime(&now);

    // 3. Format the time: %d (day), %b (abbreviated month), %Y (year), etc.

    return strftime(write_buf, cap, "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", gmt_info);
}

static int response_header_connection_write(char * write_buf, const size_t cap, const int keep_alive) {
    const char * format = keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    return snprintf(write_buf, cap, format);
}

ssize_t response_serialize(const HttpResponse * resp, char * buffer, const size_t buffer_size, const int keep_alive) {
    ssize_t offset = 0;

    // Start line
    int written = snprintf(buffer + offset, buffer_size - offset,
        "%s %d %s\r\n", "HTTP/1.1", resp->status, resp->reason);

    if (written < 0 || (size_t)written > buffer_size - offset) return -1;
    offset += written;

    // current time in GMT
    const size_t time_written = response_current_time_write(buffer + offset, buffer_size - offset);
    if (time_written == 0) return -1;
    offset += time_written;

    // Headers
    int saw_content_length = 0;
    for (int i = 0; i < resp->header_count; i++) {
        written = response_header_write(buffer + offset, buffer_size - offset, &resp->headers[i]);
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

    written = response_header_connection_write(buffer + offset, buffer_size - offset, keep_alive);
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

SendReponseStatus response_send(const int fd, const HttpBuffer * resp, SendSt * st) {
    assert(resp);
    assert(resp->buffer);
    assert(st);
    assert(fd >= 0);
    assert(st->sent < resp->size);

    SendReponseStatus res = SEND_HAS_MORE;

    // MSG_NOSIGNAL handles SIGPIPE killing the process when the peer has closed.
    const ssize_t sent = send(fd, resp->buffer + st->sent, resp->size - st->sent, MSG_NOSIGNAL);

    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return res;
        if (errno == EPIPE || errno == ECONNRESET) res = SEND_PEER_CLOSED;
        else res = SEND_ERROR;
        return res;
    }

    st->sent += sent;

    if (st->sent == resp->size) res = SEND_OK;

    return res;
}

HttpResponse response_error_from_status(const ParseStatus s) {
    static const HttpResponse r400 = { .status = 400, .reason = "Bad Request",
                                       .body = "Bad Request", .body_len = 11 };
    static const HttpResponse r413 = { .status = 413, .reason = "Payload Too Large",
                                       .body = "Payload too large", .body_len = 17 };
    static const HttpResponse r414 = { .status = 414, .reason = "URI Too Long",
                                       .body = "URI Too Long", .body_len = 12 };
    static const HttpResponse r431 = { .status = 431, .reason = "Request Header Fields Too Large",
                                       .body = "Request Header Fields Too Large", .body_len = 31 };
    static const HttpResponse r500 = { .status = 500, .reason = "Internal Server Error",
                                       .body = "Internal Server Error", .body_len = 21 };
    static const HttpResponse r501 = { .status = 501, .reason = "Not Implemented",
                                       .body = "Not Implemented", .body_len = 15 };
    static const HttpResponse r505 = { .status = 505, .reason = "HTTP Version Not Supported",
                                       .body = "HTTP Version Not Supported", .body_len = 26 };
    static const HttpResponse r404 = { .status = 404, .reason = "Not Found",
                                       .body = "Not Found", .body_len = 9 };

    switch (s) {
        case PARSE_BAD_REQUEST:           return r400;
        case PARSE_PAYLOAD_TOO_LARGE:     return r413;
        case PARSE_URI_TOO_LONG:          return r414;
        case PARSE_HEADER_KEY_TOO_LONG:
        case PARSE_HEADER_VALUE_TOO_LONG:
        case PARSE_HEADER_TOO_LONG:       return r431;
        case PARSE_NOT_IMPLEMENTED:       return r501;
        case PARSE_VERSION_NOT_SUPPORTED: return r505;
        case PARSE_NOT_FOUND:             return r404;
        default:                          return r500;
    }
}

HttpResponse response_error_405(const char * const *allowed, const size_t allowed_count, const HttpBuffer * allow_buf, ResponseHeader *h) {
    for (size_t i = 0; i < allowed_count; i++) {
        if (i > 0) strncat(allow_buf->buffer, ", ", allow_buf->cap - strlen(allow_buf->buffer) - 1);
        strncat(allow_buf->buffer, allowed[i], allow_buf->cap - strlen(allow_buf->buffer) - 1);
    }
    h->key = "Allow";
    h->value = allow_buf->buffer;
    return (HttpResponse) {
        .status = 405,                .reason = "Method Not Allowed",
        .body = "Method Not Allowed", .body_len = 18,
        .headers = h,                 .header_count = 1
    };
}

void response_error_serialize(HttpBuffer * resp, const ParseStatus s) {
    const HttpResponse res = response_error_from_status(s);

    // error responses generally close, so keep-alive is set to 0
    resp->size = response_serialize(&res, resp->buffer, resp->cap, 0);
}

HttpResponse response_none(const int status, const char *reason) {
    return (HttpResponse) {
        .status = status,
        .reason = reason,
        .kind = BODY_NONE,
    };
}
