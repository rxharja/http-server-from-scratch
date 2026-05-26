//
// Created by redonxharja on 5/4/26.
//

#include "Connection.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <asm-generic/errno-base.h>
#include <sys/socket.h>

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

ReadHeaderResult recv_header(const int fd, HttpBuffer * req) {
    assert(req);
    assert(req->buffer);
    assert(fd >= 0);

    ReadHeaderResult res = {0};
    res.status = READ_HEADER_HAS_MORE;
    res.total_received = (ssize_t)req->size;

    while (1) {
        if (res.total_received >= req->cap) {
            res.status = READ_HEADER_TOO_LARGE; // return 431
            break;
        }

        const ssize_t got = recv(fd, req->buffer + req->size, req->cap - req->size, 0);

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

        const char *terminator = memmem( req->buffer, res.total_received, "\r\n\r\n", 4);

        if (terminator) {
            res.status = READ_HEADER_OK;
            res.body_start = terminator - req->buffer + 4; // pointer end - pointer start + 4
            break;
        }
    }

    req->size = res.total_received;
    return res;
}

ReadBodyResult recv_body(const int fd, HttpBuffer * req, CLBodySt * st) {
    assert(req);
    assert(req->buffer);
    assert(st);
    assert(fd >= 0);

    ReadBodyResult res = {0};
    res.status = READ_BODY_HAS_MORE;

    while (1) {
        if (req->size >= req->cap) {
            res.status = READ_BODY_TOO_LARGE;
            break;
        }

        // body length is not the size of the buffer but the length provided by the header "Content-Length"
        if (st->received >= st->body_len) {
            res.status = READ_BODY_OK;

            // reading past body_len means we're reading into the start of the next request
            if (st->received > st->body_len) res.next_req_offset = st->body_start + st->body_len;
            break;
        }

        const ssize_t got = recv(fd, req->buffer + req->size, req->cap - req->size, 0);

        if (got == 0) {
            res.status = READ_BODY_PEER_CLOSED;
            break;
        }
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // has more, we're coming back here next poll loop
            if (errno == EINTR) continue;
            res.status = READ_BODY_IO_ERROR;
            break;
        }

        req->size += got;
        st->received += got;
    }

    // has more, we're coming back here next poll loop
    return res;
}

// todo: rework to state machine for resuming where we left off between polls
ReadBodyResult recv_chunked_body(Connection * conn) {
    // ReadBodyResult res = {0};
    // res.status = READ_BODY_HAS_MORE;
    //
    // while (1) {
    //     const ChunkResult dechunk_res = body_dechunk(
    //         conn->req.http_buffer.buffer,
    //         conn->req.http_buffer.buffer + conn->req.already_have,
    //         conn->body_dechunked.buffer, conn->body_dechunked.cap);
    //
    //     switch (dechunk_res.parse_result.status) {
    //     case PARSE_OK:
    //         res.status = READ_BODY_OK;
    //         res.body_received = dechunk_res.chunk_size;
    //         return res;
    //     case PARSE_INCOMPLETE:
    //         res.body_received = dechunk_res.chunk_size;
    //         break; // continue recv-ing
    //     default:
    //         res.status = READ_BODY_BAD_DATA;
    //         return res;
    //     }
    //
    //     if (conn->req.already_have >= conn->req.http_buffer.cap) {
    //         res.status = READ_BODY_TOO_LARGE;
    //         break;
    //     }
    //
    //     const ssize_t got = recv(conn->fd,
    //         conn->req.http_buffer.buffer + conn->req.already_have,
    //         conn->req.http_buffer.cap - conn->req.already_have, 0);
    //
    //     if (got == 0) {
    //         res.status = READ_BODY_PEER_CLOSED;
    //         break;
    //     }
    //
    //     if (got < 0) { // -1
    //         if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    //         if (errno == EINTR) continue;
    //         res.status = READ_BODY_IO_ERROR;
    //         break;
    //     }
    //     conn->req.already_have += got;
    // }
    //
    // return res;
}

SendReponseStatus send_response(const int fd, const HttpBuffer * resp, SendSt * st) {
    assert(resp);
    assert(resp->buffer);
    assert(st);
    assert(fd >= 0);
    assert(st->sent < resp->size);

    SendReponseStatus res;
    res = SEND_HAS_MORE;

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

static HttpResponse to_http_error(const ParseStatus s) {
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

static void serialize_error(const HttpBuffer * resp, const ParseStatus s) {
    const HttpResponse res = to_http_error(s);
    serialize_response(&res, resp->buffer, resp->cap, 0);
}

static ConnPhase step_read_header(Connection * conn) {
    assert(conn);
    assert(conn->phase == CONN_READING_REQUEST);

    const ReadHeaderResult header_res = recv_header(conn->fd, &conn->req_buf);
    if (header_res.status == READ_HEADER_HAS_MORE) return CONN_READING_REQUEST;
    if (header_res.status == READ_HEADER_PEER_CLOSED || header_res.status == READ_HEADER_IO_ERROR) return CONN_CLOSED;
    if (header_res.status != READ_HEADER_OK) {
        serialize_error(&conn->resp_buf, header_res.status == READ_HEADER_TOO_LARGE ? PARSE_HEADER_TOO_LONG : PARSE_BAD_REQUEST);
        return CONN_SENDING_RESPONSE;
    }

    const ParseResult parse_res = parse_request(conn->req_buf.buffer, conn->req_buf.size, &conn->req_parsed);
    if (parse_res.status != PARSE_OK) {
        serialize_error(&conn->resp_buf, parse_res.status);
        return CONN_SENDING_RESPONSE;
    }

    assert(header_res.body_start >= 0);
    assert(header_res.total_received >= header_res.body_start);

    if (!ascii_ieq(conn->req_parsed.request_line.version, "http/1.0")) {
        if (!get_header(conn->req_parsed.headers, conn->req_parsed.header_count, "host")) {
            serialize_error(&conn->resp_buf, PARSE_BAD_REQUEST);
            return CONN_SENDING_RESPONSE;
        }

        // default to keep alive if we don't have a connection header
        const Header * ka_header = get_header(conn->req_parsed.headers, conn->req_parsed.header_count, "connection");
        if (!ka_header || ascii_ieq("keep-alive", ka_header->value)) conn->keep_alive = 1;
    }

    TransferCoding coding = TE_NONE;
    ParseStatus te_status = PARSE_OK;
    for (int i = 0; i < conn->req_parsed.header_count; i++) {
        if (coding == TE_UNSUPPORTED || te_status != PARSE_OK) break;
        if (!ascii_ieq(conn->req_parsed.headers[i].key, "transfer-encoding")) continue;
        te_status = parse_transfer_encoding(conn->req_parsed.headers[i].value, &coding);
    }

    if (coding == TE_UNSUPPORTED) {
        serialize_error(&conn->resp_buf, PARSE_NOT_IMPLEMENTED);
        return CONN_SENDING_RESPONSE;
    }

    if (te_status != PARSE_OK) {
        serialize_error(&conn->resp_buf, te_status);
        return CONN_SENDING_RESPONSE;
    }

    const Header *ct_len_h = get_header(conn->req_parsed.headers, conn->req_parsed.header_count, "content-length");

    // prevent smuggling by returning 400
    if (ct_len_h && coding == TE_CHUNKED) {
        serialize_error(&conn->resp_buf, PARSE_BAD_REQUEST);
        return CONN_SENDING_RESPONSE;
    }

    size_t body_len = 0;
    if (ct_len_h) {
        const ParseStatus ps = parse_uint(ct_len_h->value, strlen(ct_len_h->value), 10, MAX_BODY_LEN, &body_len);
        if (ps != PARSE_OK) {
            serialize_error(&conn->resp_buf, ps);
            return CONN_SENDING_RESPONSE;
        }
    }

    const int has_body = coding == TE_CHUNKED || (ct_len_h && body_len > 0);
    if (conn->req_parsed.request_line.method == HEAD && has_body) {
        // Per RFC: clients MUST NOT send content with HEAD.
        serialize_error(&conn->resp_buf, PARSE_BAD_REQUEST);
        return CONN_SENDING_RESPONSE;
    }

    // dispatch
    if (coding == TE_CHUNKED) {
        // todo: handle ChunkedBodySt
        return CONN_READING_BODY_CHUNKED;
    }

    if (!ct_len_h || body_len == 0) {
        conn->req_parsed.body_len = 0;
        return CONN_BUILDING;
    }

    assert(body_len > 0);
    assert((size_t)header_res.body_start <= conn->req_buf.size);

    conn->st.cl = (CLBodySt) {
        .body_len   = body_len,
        .received   = header_res.total_received - header_res.body_start,
        .body_start = header_res.body_start,
    };

    return CONN_READING_BODY_CL;
}

static ConnPhase step_send_response(Connection * conn) {
    const SendReponseStatus status = send_response(conn->fd, &conn->resp_buf, &conn->st.send);
    switch (status) {
        case SEND_HAS_MORE:    return CONN_SENDING_RESPONSE;
        case SEND_ERROR:       return CONN_CLOSED;
        case SEND_PEER_CLOSED: return CONN_CLOSED;
        case SEND_OK:          return CONN_CLOSED;
    }
}

KeepAliveStatus handle_connection(Connection * conn, const Router * router) {
    switch (conn->phase) {
        case CONN_READING_REQUEST:
            conn->phase = step_read_header(conn);
            break;
        case CONN_READING_BODY_CL:
            break;
        case CONN_READING_BODY_CHUNKED:
            serialize_error(&conn->resp_buf, PARSE_NOT_IMPLEMENTED);
            conn->phase = CONN_SENDING_RESPONSE;
            break;
        case CONN_BUILDING:
            break;
        case CONN_SENDING_RESPONSE:
            conn->phase = step_send_response(conn);
            break;
    }
}

// KeepAliveStatus handle_connection(Connection * conn, const Router * router) {
    // KeepAliveStatus status = {0};
    // HttpResponse res = to_http_error(PARSE_BAD_REQUEST);
    // ResponseHeader allow_h = {0};
    // char res_allow_buf[128] = {0};
    // HttpBuffer allow_buf = {.buffer = res_allow_buf, .cap = 128 };
    // HttpRequest *req = calloc(1, sizeof(HttpRequest));
    // if (!req) goto serve;

    // const ReadHeaderResult header_res = recv_header(conn->fd, &conn->req_buf);
    // if (header_res.status != READ_HEADER_OK) {
    //     res = to_http_error(header_res.status == READ_HEADER_TOO_LARGE
    //         ? PARSE_HEADER_TOO_LONG : PARSE_BAD_REQUEST);
    //     goto serve;
    // }

    // conn->req_buf.size = header_res.total_received;

    // we don't increment buffer size here of the request because we're not reading anymore from the connection
    // const ParseResult parse_req_res = parse_request(conn->req_buf.buffer, conn->req_buf.size, req);
    // if (parse_req_res.status != PARSE_OK) {
    //     res = to_http_error(parse_req_res.status);
    //     goto serve;
    // }

    // HTTP/1.1 requires having a host header
    // if (!ascii_ieq(req->request_line.version, "http/1.0")) {
    //     const Header * host_header = get_header(req->headers, req->header_count, "host");
    //     if (!host_header) { res = to_http_error(PARSE_BAD_REQUEST); goto serve; }
    //
    //     // Keep-Alive or close decision, next req offset set.
    //     const Header * ka_header = get_header(req->headers, req->header_count, "connection");
    //     // default to keep alive if we don't have a connection header
    //     if (!ka_header || ascii_ieq("keep-alive", ka_header->value)) status.keep_alive = 1;
    //     // otherwise we stay at 0 since it will be connection: close
    // }

    // TransferCoding coding = TE_NONE;
    // ParseStatus te_status = PARSE_OK;
    // for (int i = 0; i < req->header_count; i++) {
    //     if (coding == TE_UNSUPPORTED || te_status != PARSE_OK) break;
    //     if (!ascii_ieq(req->headers[i].key, "transfer-encoding")) continue;
    //     te_status = parse_transfer_encoding(req->headers[i].value, &coding);
    // }

    // if (coding == TE_UNSUPPORTED) { res = to_http_error(PARSE_NOT_IMPLEMENTED); goto serve; }
    // if (te_status != PARSE_OK)    { res = to_http_error(te_status);               goto serve; }

//     ReadBodyResult body_res = {0};
//     if (coding == TE_CHUNKED) {
//         body_res = recv_chunked_body(conn);
//         if (body_res.status != READ_BODY_OK) { res = to_http_error(PARSE_BAD_REQUEST); goto serve; }
//         req->body_len = body_res.body_received;
//     }
//     else {
//         const Header *ct_len_h = get_header(req->headers, req->header_count, "content-length");
//         if (!ct_len_h) req->body_len = 0;
//         else {
//             size_t body_len = 0;
//             const ParseStatus ps = parse_uint(ct_len_h->value, strlen(ct_len_h->value), 10, MAX_BODY_LEN, &body_len);
//             if (ps != PARSE_OK) { res = to_http_error(ps); goto serve; }
//             body_res = recv_body(conn, body_len);
//
//             if (body_res.status != READ_BODY_OK && body_res.status != READ_BODY_HAS_MORE) {
//                 res = to_http_error(PARSE_BAD_REQUEST);
//                 goto serve;
//             }
//
//             req->body_len = body_res.body_received;
//         }
//     }
//
//     // absolute offset before next response
//     if (body_res.status == READ_BODY_HAS_MORE) status.next_req_offset = header_res.body_start + body_res.next_req_offset;
//     else if (req->body_len == 0 && header_res.total_received > header_res.body_start) status.next_req_offset = header_res.body_start;
//
//     const HttpMethod method = req->request_line.method == HEAD ? GET : req->request_line.method;
//
//     if (method == GET) {
//         CachedFile * sf = dict_find(router->static_cache, req->request_line.path);
//         if (sf) { res = from_cached_file(req, sf); goto serve; }
//
//         const DynamicLookupResult d = dynamic_lookup(router->dynamic_cache, req, req->request_line.path);
//         switch (d.status) {
//             case DYN_NOT_REGISTERED:                                           break;
//             case DYN_GONE:          res = to_http_error(PARSE_NOT_FOUND); goto serve;
//             case DYN_NOT_MODIFIED:  res = make_304(d.file);                    goto serve;
//             case DYN_HIT:           res = from_dynamic_cached_file(d.file);    goto serve;
//         }
//     }
//
//     const RouteLookupResult route_res = route_lookup(router->routes, router->route_count, show_http_method(method), req->request_line.path);
//
//     const Route *route = route_res.route;
//     if      (route && !route->data)       res = route_res.route->handler.fn(req);
//     else if (route && route->data)        res = route_res.route->handler.fn_with_data(req, route->data);
//     else if (route_res.allowed_count > 0) res = synthesize_405(route_res.allowed, route_res.allowed_count, &allow_buf, &allow_h);
//     else                                  res = to_http_error(PARSE_NOT_FOUND);
//
//     if (req->request_line.method == HEAD) res.head_only = 1;
//
//     show_request(req);
//
// serve: ;
//     status.bytes_to_send = (int)serialize_response(&res, conn->resp.buffer, conn->resp.cap, status.keep_alive);
//     conn->resp.size = status.bytes_to_send;
//     free(req);
//     return status;
// }
