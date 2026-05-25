//
// Created by redonxharja on 5/4/26.
//

#include "Connection.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <asm-generic/errno-base.h>
#include "http_server/HttpRequest.h"
#include "http_server/HttpResponse.h"
#include "http_server/ParseResult.h"
#include "parser.h"

struct addrinfo;

int valid_port(const char * str) {
    char *endptr;
    errno = 0;

    const long num = strtol(str, &endptr, 10);

    if (errno != 0 || *endptr != '\0' || endptr == str) return 1;
    if (num <= 0 || num > 65535) return 1;
    return 0;
}

ReadHeaderResult recv_header(Connection *conn) {
    // assert(conn);
    // assert(conn->req.http_buffer.buffer);

    ReadHeaderResult res = {0};
    res.status = READ_HEADER_HAS_MORE;
    res.total_received = (ssize_t)conn->req.already_have;

    while (1) {
        if (res.total_received >= conn->req.http_buffer.cap) {
            res.status = READ_HEADER_TOO_LARGE; // return 431
            break;
        }

        const ssize_t got = recv(conn->fd,
            conn->req.http_buffer.buffer + conn->req.already_have,
            conn->req.http_buffer.cap - conn->req.already_have, 0);

        if (got == 0) {
            res.status = READ_HEADER_PEER_CLOSED; // 400
            break;
        }

        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // READ_HEADER_HAS_MORE
            if (errno == EINTR) continue;
            res.status = READ_HEADER_IO_ERROR;
            break;
        }

        res.total_received += got;

        const char *terminator = memmem(
            conn->req.http_buffer.buffer,
            res.total_received, "\r\n\r\n", 4);

        if (terminator) {
            res.status = READ_HEADER_OK;
            res.body_start = terminator - conn->req.http_buffer.buffer + 4; // pointer end - pointer start + 4
            break;
        }
    }

    conn->req.already_have = res.total_received;
    return res;
}

// todo: rework to state machine for resuming where we left off between polls
ReadBodyResult recv_chunked_body(Connection * conn) {
    ReadBodyResult res = {0};
    res.status = READ_BODY_HAS_MORE;

    while (1) {
        const ChunkResult dechunk_res = body_dechunk(
            conn->req.http_buffer.buffer,
            conn->req.http_buffer.buffer + conn->req.already_have,
            conn->body_dechunked.buffer, conn->body_dechunked.cap);

        switch (dechunk_res.parse_result.status) {
            case PARSE_OK:
                res.status = READ_BODY_OK;
                res.body_received = dechunk_res.chunk_size;
                return res;
            case PARSE_INCOMPLETE:
                res.body_received = dechunk_res.chunk_size;
                break; // continue recv-ing
            default:
                res.status = READ_BODY_BAD_DATA;
                return res;
        }

        if (conn->req.already_have >= conn->req.http_buffer.cap) {
            res.status = READ_BODY_TOO_LARGE;
            break;
        }

        const ssize_t got = recv(conn->fd,
            conn->req.http_buffer.buffer + conn->req.already_have,
            conn->req.http_buffer.cap - conn->req.already_have, 0);

        if (got == 0) {
            res.status = READ_BODY_PEER_CLOSED;
            break;
        }

        if (got < 0) { // -1
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            res.status = READ_BODY_IO_ERROR;
            break;
        }
        conn->req.already_have += got;
    }

    return res;
}

ReadBodyResult recv_body(Connection * conn, const size_t body_len) {
    ReadBodyResult res = {0};
    res.status = READ_BODY_HAS_MORE;

    const size_t body_in_buf = conn->req.already_have - conn->body_start;
    res.body_received = body_in_buf < body_len ? body_in_buf : body_len;

    while (1) {
        if (conn->req.already_have >= conn->req.http_buffer.cap) {
            res.status = READ_BODY_TOO_LARGE;
            break;
        }

        if (res.body_received >= body_len) {
            res.status = READ_BODY_OK;
            if (body_in_buf > body_len) res.next_req_offset = conn->body_start + body_len;
            break;
        }

        const ssize_t got = recv(conn->fd,
            conn->req.http_buffer.buffer + conn->req.already_have,
            conn->req.http_buffer.cap - conn->req.already_have, 0);

        if (got == 0) {
            res.status = READ_BODY_PEER_CLOSED;
            break;
        }
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            res.status = READ_BODY_IO_ERROR;
            break;
        }

        conn->req.already_have += got;
        res.body_received += got;
    }

    return res;
}

static HttpResponse to_http_response(const ParseStatus s) {
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

HttpResponse synthesize_405(const char * const *allowed, const size_t allowed_count, const HttpBuffer * allow_buf, ResponseHeader *h) {
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

KeepAliveStatus handle_connection(Connection * conn, const Router * router) {
    KeepAliveStatus status = {0};
    HttpResponse res = to_http_response(PARSE_BAD_REQUEST);
    ResponseHeader allow_h = {0};
    char res_allow_buf[128] = {0};
    HttpBuffer allow_buf = {.buffer = res_allow_buf, .cap = 128 };
    HttpRequest *req = calloc(1, sizeof(HttpRequest));
    if (!req) goto serve;

    const ReadHeaderResult header_res = recv_header(conn);
    if (header_res.status != READ_HEADER_OK) {
        res = to_http_response(header_res.status == READ_HEADER_TOO_LARGE
            ? PARSE_HEADER_TOO_LONG : PARSE_BAD_REQUEST);
        goto serve;
    }

    conn->req.http_buffer.size = header_res.total_received;

    // we don't increment buffer size here of the request because we're not reading anymore from the connection
    const ParseResult parse_req_res = parse_request(conn->req.http_buffer.buffer, conn->req.http_buffer.size, req);
    if (parse_req_res.status != PARSE_OK) {
        res = to_http_response(parse_req_res.status);
        goto serve;
    }

    // HTTP/1.1 requires having a host header
    if (!ascii_ieq(req->request_line.version, "http/1.0")) {
        const Header * host_header = get_header(req->headers, req->header_count, "host");
        if (!host_header) { res = to_http_response(PARSE_BAD_REQUEST); goto serve; }

        // Keep-Alive or close decision, next req offset set.
        const Header * ka_header = get_header(req->headers, req->header_count, "connection");
        if (!ka_header || ascii_ieq("keep-alive", ka_header->value)) status.keep_alive = 1;
        // otherwise we stay at 0 since it will be connection: close
    }


    TransferCoding coding = TE_NONE;
    ParseStatus te_status = PARSE_OK;
    for (int i = 0; i < req->header_count; i++) {
        if (coding == TE_UNSUPPORTED || te_status != PARSE_OK) break;
        if (!ascii_ieq(req->headers[i].key, "transfer-encoding")) continue;
        te_status = parse_transfer_encoding(req->headers[i].value, &coding);
    }

    if (coding == TE_UNSUPPORTED) { res = to_http_response(PARSE_NOT_IMPLEMENTED); goto serve; }
    if (te_status != PARSE_OK)    { res = to_http_response(te_status);               goto serve; }

    ReadBodyResult body_res = {0};
    if (coding == TE_CHUNKED) {
        body_res = recv_chunked_body(conn);
        if (body_res.status != READ_BODY_OK) { res = to_http_response(PARSE_BAD_REQUEST); goto serve; }
        req->body_len = body_res.body_received;
    }
    else {
        const Header *ct_len_h = get_header(req->headers, req->header_count, "content-length");
        if (!ct_len_h) req->body_len = 0;
        else {
            size_t body_len = 0;
            const ParseStatus ps = parse_uint(ct_len_h->value, strlen(ct_len_h->value), 10, MAX_BODY_LEN, &body_len);
            if (ps != PARSE_OK) { res = to_http_response(ps); goto serve; }
            body_res = recv_body(conn, body_len);

            if (body_res.status != READ_BODY_OK && body_res.status != READ_BODY_HAS_MORE) {
                res = to_http_response(PARSE_BAD_REQUEST);
                goto serve;
            }

            req->body_len = body_res.body_received;
        }
    }

    // absolute offset before next response
    if (body_res.status == READ_BODY_HAS_MORE) status.next_req_offset = header_res.body_start + body_res.next_req_offset;
    else if (req->body_len == 0 && header_res.total_received > header_res.body_start) status.next_req_offset = header_res.body_start;

    const HttpMethod method = req->request_line.method == HEAD ? GET : req->request_line.method;

    if (method == GET) {
        CachedFile * sf = dict_find(router->static_cache, req->request_line.path);
        if (sf) { res = from_cached_file(req, sf); goto serve; }

        const DynamicLookupResult d = dynamic_lookup(router->dynamic_cache, req, req->request_line.path);
        switch (d.status) {
            case DYN_NOT_REGISTERED:                                           break;
            case DYN_GONE:          res = to_http_response(PARSE_NOT_FOUND); goto serve;
            case DYN_NOT_MODIFIED:  res = make_304(d.file);                    goto serve;
            case DYN_HIT:           res = from_dynamic_cached_file(d.file);    goto serve;
        }
    }

    const RouteLookupResult route_res = route_lookup(router->routes, router->route_count, show_http_method(method), req->request_line.path);

    const Route *route = route_res.route;
    if      (route && !route->data)       res = route_res.route->handler.fn(req);
    else if (route && route->data)        res = route_res.route->handler.fn_with_data(req, route->data);
    else if (route_res.allowed_count > 0) res = synthesize_405(route_res.allowed, route_res.allowed_count, &allow_buf, &allow_h);
    else                                  res = to_http_response(PARSE_NOT_FOUND);

    if (req->request_line.method == HEAD) res.head_only = 1;

    show_request(req);

serve: ;
    status.bytes_to_send = (int)serialize_response(&res, conn->resp.buffer, conn->resp.cap, status.keep_alive);
    conn->resp.size = status.bytes_to_send;
    free(req);
    return status;
}
