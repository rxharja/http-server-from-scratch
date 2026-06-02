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

    if (!saw_content_length && resp->body.body_buf.size > 0) {
        written = snprintf(buffer + offset, buffer_size - offset,
            "Content-Length: %zu\r\n", resp->body.body_buf.size);
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
    if (!resp->head_only && resp->body.body_buf.buffer && resp->body.body_buf.size > 0) {
        if (offset + resp->body.body_buf.size > buffer_size) return -1;
        memcpy(buffer + offset, resp->body.body_buf.buffer, resp->body.body_buf.size);
        offset += resp->body.body_buf.size;
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

#define R_400 "Bad Request"
#define R_404 "Not Found"
#define R_405 "Method Not Allowed"
#define R_413 "Payload Too Large"
#define R_414 "URI Too Long"
#define R_431 "Request Header Fields Too Large"
#define R_500 "Internal Server Error"
#define R_501 "Not Implemented"
#define R_505 "HTTP Version Not Supported"

HttpResponse response_error_from_status(const ParseStatus s) {
    switch (s) {
        case PARSE_BAD_REQUEST:           return response_buffer(400, R_400, R_400, sizeof R_400, NULL, 0);
        case PARSE_PAYLOAD_TOO_LARGE:     return response_buffer(413, R_413, R_413, sizeof R_413, NULL, 0);
        case PARSE_URI_TOO_LONG:          return response_buffer(414, R_414, R_414, sizeof R_414, NULL, 0);
        case PARSE_HEADER_KEY_TOO_LONG:
        case PARSE_HEADER_VALUE_TOO_LONG:
        case PARSE_HEADER_TOO_LONG:       return response_buffer(431, R_431, R_431, sizeof R_431, NULL, 0);
        case PARSE_NOT_IMPLEMENTED:       return response_buffer(501, R_501, R_501, sizeof R_501, NULL, 0);
        case PARSE_VERSION_NOT_SUPPORTED: return response_buffer(505, R_505, R_505, sizeof R_505, NULL, 0);
        case PARSE_NOT_FOUND:             return response_buffer(404, R_404, R_404, sizeof R_404, NULL, 0);
        default:                          return response_buffer(500, R_500, R_500, sizeof R_500, NULL, 0);
    }
}

HttpResponse response_error_405(const char * const *allowed, const size_t allowed_count, const HttpBuffer * allow_buf, ResponseHeader *h) {
    for (size_t i = 0; i < allowed_count; i++) {
        if (i > 0) strncat(allow_buf->buffer, ", ", allow_buf->cap - strlen(allow_buf->buffer) - 1);
        strncat(allow_buf->buffer, allowed[i], allow_buf->cap - strlen(allow_buf->buffer) - 1);
    }
    h->key = "Allow";
    h->value = allow_buf->buffer;
    return response_buffer(405, R_405, R_405, sizeof R_405, h, 1);
}

void response_error_serialize(HttpBuffer * resp, const ParseStatus s) {
    const HttpResponse res = response_error_from_status(s);

    // error responses generally close, so keep-alive is set to 0
    resp->size = response_serialize(&res, resp->buffer, resp->cap, 0);
}

HttpResponse response_none(const int status, const char *reason, const ResponseHeader *headers,
                           const size_t header_count) {
    return (HttpResponse){
        .status = status, .reason = reason,
        .headers = headers, .header_count = header_count,
        .kind = BODY_NONE,
    };
}

HttpResponse response_buffer(const int status, const char *reason, const char *body, const size_t len,
                             const ResponseHeader *headers, const size_t header_count) {
    return (HttpResponse){
        .status = status, .reason = reason,
        .headers = headers, .header_count = header_count,
        .kind = BODY_BUFFER, .body.body_buf = (HttpBuffer){
            .buffer = (char *) body,
            .size = len,
            .cap = len,
        }
    };
}

ssize_t chunk_frame(const char * payload, const size_t len, char * out, const size_t out_cap) {
    const int n = snprintf(out, out_cap, "%zx\r\n", len); // hex size
    if (n < 0 || (size_t)n >= out_cap) return -1; // truncated snprintf or errored
    if ((size_t)n + len + 2 > out_cap) return -1; // payload + trailing CRLF won't fit
    memcpy(out + n, payload, len);
    out[n + len] = '\r';
    out[n + len + 1] = '\n';
    return n + len + 2;
}

ssize_t chunk_frame_last (char * out, const size_t out_cap) {
    if (out_cap < 5) return -1;
    memcpy(out, "0\r\n\r\n", 5);
    return 5;
}