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
#include "http_server/HttpServer.h"

struct addrinfo;

ReadHeaderResult conn_recv_header(const int fd, HttpBuffer * req) {
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

ReadBodyResult conn_recv_body_cl(const int fd, HttpBuffer * req, CLBodySt * st) {
    assert(req);
    assert(req->buffer);
    assert(st);
    assert(fd >= 0);

    ReadBodyResult res = {0};
    res.status = READ_BODY_HAS_MORE;

    while (1) {
        // body length is not the size of the buffer but the length provided by the header "Content-Length"
        if (st->received >= st->body_len) {
            res.status = READ_BODY_OK;

            // reading past body_len means we're reading into the start of the next request
            if (st->received > st->body_len) res.next_req_offset = st->body_start + st->body_len;
            break;
        }

        if (req->size >= req->cap) {
            res.status = READ_BODY_TOO_LARGE;
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

ReadBodyResult conn_recv_body_chunked(const int fd, HttpBuffer *req_buf, HttpBuffer *dechunked, ChunkedBodySt * st) {
    assert(fd > 0);
    assert(req_buf);
    assert(req_buf->buffer);
    assert(dechunked);
    assert(dechunked->buffer);
    assert(st);
    assert(st->body_start + st->consumed <= req_buf->size);

    ReadBodyResult res = {0};

    while (1) {
        // drain whatever's already buffered, advancing the decoder until it asks for more
        while (1) {
            const char  *cur    = req_buf->buffer + st->body_start + st->consumed;
            const size_t in_len = req_buf->size - (st->body_start + st->consumed);

            const ChunkResult cr = chunk_advance(
                &st->dec, cur, in_len,
                dechunked->buffer + dechunked->size,
                dechunked->cap - dechunked->size);

            // increment consumed by the previous version of the cursor to where next has ended up
            // note this is raw bytes and not decoded bytes.
            if (cr.parse_result.next) st->consumed += (size_t)(cr.parse_result.next - cur);
            dechunked->size += cr.bytes_written;

            if (cr.parse_result.status == PARSE_PAYLOAD_TOO_LARGE) { res.status = READ_BODY_TOO_LARGE; return res; }
            if (cr.parse_result.status != PARSE_OK && cr.parse_result.status != PARSE_INCOMPLETE) {
                res.status = READ_BODY_BAD_DATA;
                return res;
            }

            if (st->dec.phase == CHUNK_DONE) {
                res.status = READ_BODY_OK;
                res.next_req_offset = st->body_start + st->consumed;
                return res;
            }

            if (cr.parse_result.status == PARSE_INCOMPLETE) break;   // need more wire bytes
            // PARSE_OK but not DONE: more might be buffered — loop and try again
        }

        // PARSE_INCOMPLETE: need to recv more. If the buffer is already full,
        // a single header line is wider than req_buf — surface as 413.
        if (req_buf->size >= req_buf->cap) { res.status = READ_BODY_TOO_LARGE; return res; }

        const ssize_t got = recv(fd, req_buf->buffer + req_buf->size, req_buf->cap - req_buf->size, 0);
        if (got == 0) { res.status = READ_BODY_PEER_CLOSED; return res; }
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { res.status = READ_BODY_HAS_MORE; return res; }
            if (errno == EINTR) continue;   // re-drain (idempotent), then retry recv
            res.status = READ_BODY_IO_ERROR;
            return res;
        }

        req_buf->size += got;
    }
}

static SendReponseStatus response_send(const int fd, const HttpBuffer * resp, SendSt * st) {
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

static HttpResponse response_error_from_status(const ParseStatus s) {
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

static void response_error_serialize(HttpBuffer * resp, const ParseStatus s) {
    const HttpResponse res = response_error_from_status(s);
    // error responses generally close, so keep-alive is set to 0
    resp->size = response_serialize(&res, resp->buffer, resp->cap, 0);
}

static ConnPhase phase_send_begin(Connection *conn) {
    conn->st.send = (SendSt){ .sent = 0 };
    return CONN_SENDING_RESPONSE;
}

static ConnPhase step_header_read(Connection * conn) {
    assert(conn);
    assert(conn->phase == CONN_READING_REQUEST);

    const ReadHeaderResult header_res = conn_recv_header(conn->fd, &conn->req_buf);
    if (header_res.status == READ_HEADER_HAS_MORE) return CONN_READING_REQUEST;
    if (header_res.status == READ_HEADER_PEER_CLOSED || header_res.status == READ_HEADER_IO_ERROR) return CONN_CLOSED;
    if (header_res.status != READ_HEADER_OK) {
        const int err_type = header_res.status == READ_HEADER_TOO_LARGE ? PARSE_HEADER_TOO_LONG : PARSE_BAD_REQUEST;
        response_error_serialize(&conn->resp_buf, err_type);
        return phase_send_begin(conn);
    }

    const ParseResult parse_res = request_parse(conn->req_buf.buffer, conn->req_buf.size, &conn->req_parsed);
    if (parse_res.status != PARSE_OK) {
        response_error_serialize(&conn->resp_buf, parse_res.status);
        return phase_send_begin(conn);
    }

    assert(header_res.body_start >= 0);
    assert(header_res.total_received >= header_res.body_start);

    if (!ascii_ieq(conn->req_parsed.request_line.version, "http/1.0")) {
        if (!header_find(conn->req_parsed.headers, conn->req_parsed.header_count, "host")) {
            response_error_serialize(&conn->resp_buf, PARSE_BAD_REQUEST);
            return phase_send_begin(conn);
        }

        // default to keep alive if we don't have a connection header
        const Header * ka_header = header_find(conn->req_parsed.headers, conn->req_parsed.header_count, "connection");
        if (!ka_header || ascii_ieq("keep-alive", ka_header->value)) conn->keep_alive = 1;
    }

    TransferCoding coding = TE_NONE;
    ParseStatus te_status = PARSE_OK;
    for (int i = 0; i < conn->req_parsed.header_count; i++) {
        if (coding == TE_UNSUPPORTED || te_status != PARSE_OK) break;
        if (!ascii_ieq(conn->req_parsed.headers[i].key, "transfer-encoding")) continue;
        te_status = transfer_encoding_parse(conn->req_parsed.headers[i].value, &coding);
    }

    if (coding == TE_UNSUPPORTED) {
        response_error_serialize(&conn->resp_buf, PARSE_NOT_IMPLEMENTED);
        return phase_send_begin(conn);
    }

    if (te_status != PARSE_OK) {
        response_error_serialize(&conn->resp_buf, te_status);
        return phase_send_begin(conn);
    }

    const Header *ct_len_h = header_find(conn->req_parsed.headers, conn->req_parsed.header_count, "content-length");

    // prevent smuggling by returning 400
    if (ct_len_h && coding == TE_CHUNKED) {
        response_error_serialize(&conn->resp_buf, PARSE_BAD_REQUEST);
        return phase_send_begin(conn);
    }

    size_t body_len = 0;
    if (ct_len_h) {
        const ParseStatus ps = uint_parse(ct_len_h->value, strlen(ct_len_h->value), 10, MAX_BODY_LEN, &body_len);
        if (ps != PARSE_OK) {
            response_error_serialize(&conn->resp_buf, ps);
            return phase_send_begin(conn);
        }
    }

    const int has_body = coding == TE_CHUNKED || (ct_len_h && body_len > 0);
    if (conn->req_parsed.request_line.method == HEAD && has_body) {
        // Per RFC: clients MUST NOT send content with HEAD.
        response_error_serialize(&conn->resp_buf, PARSE_BAD_REQUEST);
        return phase_send_begin(conn);
    }

    // dispatch
    if (coding == TE_CHUNKED) {
        conn->st.chunked = (ChunkedBodySt) {
            .body_start = header_res.body_start,
            .dec = {0},
        };
        conn->body_dechunked.size = 0;
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

static ConnPhase step_body_cl_read(Connection * conn) {
    assert(conn);
    assert(conn->phase == CONN_READING_BODY_CL);

    const ReadBodyResult body_res = conn_recv_body_cl(conn->fd, &conn->req_buf, &conn->st.cl);

    // body_res set to this when we have read the full content length or more.
    if (body_res.status == READ_BODY_OK) {
        conn->req_parsed.body_len = conn->st.cl.received;
        conn->next_req_offset = body_res.next_req_offset;
        return CONN_BUILDING;
    }

    if (body_res.status == READ_BODY_HAS_MORE) return CONN_READING_BODY_CL;

    // fall through case is not a valid read result.
    response_error_serialize(&conn->resp_buf, PARSE_BAD_REQUEST);
    return phase_send_begin(conn);
}

static ConnPhase step_body_chunked_read(Connection * conn) {
    assert(conn);
    assert(conn->phase == CONN_READING_BODY_CHUNKED);

    const ReadBodyResult body_res = conn_recv_body_chunked(conn->fd, &conn->req_buf, &conn->body_dechunked, &conn->st.chunked);

    switch (body_res.status) {
        case READ_BODY_OK:
            conn->req_parsed.body_len = conn->body_dechunked.size;
            conn->next_req_offset = body_res.next_req_offset;
            return CONN_BUILDING;
        case READ_BODY_HAS_MORE:
            return CONN_READING_BODY_CHUNKED;
        case READ_BODY_TOO_LARGE:
            response_error_serialize(&conn->resp_buf, PARSE_PAYLOAD_TOO_LARGE);
            return phase_send_begin(conn);
        default:
            response_error_serialize(&conn->resp_buf, PARSE_BAD_REQUEST);
            return phase_send_begin(conn);
    }
}

static ConnPhase step_response_build(Connection * conn, const Router * router) {
    assert(conn);
    assert(router);
    assert(conn->phase == CONN_BUILDING);

    HttpResponse res = {0};
    const HttpRequest * req = &conn->req_parsed;
    ResponseHeader allow_h = {0};
    char allow_storage[128] = {0};
    const HttpBuffer allow_buf = { .buffer = allow_storage, .cap = sizeof(allow_storage) };

    const HttpMethod method = conn->req_parsed.request_line.method == HEAD ? GET : conn->req_parsed.request_line.method;

    if (method == GET) {
        const CachedFile * sf = dict_find(router->static_cache, req->request_line.path);
        if (sf) { res = response_cached(&conn->req_parsed, sf); goto build_resp; }

        const DynamicLookupResult d = cache_dynamic_lookup(router->dynamic_cache, req, req->request_line.path);

        switch (d.status) {
            case DYN_NOT_REGISTERED:                                       break;
            case DYN_GONE:         res = response_error_from_status(PARSE_NOT_FOUND); goto build_resp;
            case DYN_NOT_MODIFIED: res = response_dynamic_304(d.file);                 goto build_resp;
            case DYN_HIT:          res = response_dynamic(d.file); goto build_resp;
        }
    }

    const RouteLookupResult route_res =
        route_lookup(router->routes, router->route_count, http_method_show(method), conn->req_parsed.request_line.path);

    const Route *route = route_res.route;
    if      (route && !route->data)       res = route_res.route->handler.fn(req);
    else if (route && route->data)        res = route_res.route->handler.fn_with_data(req, route->data);
    else if (route_res.allowed_count > 0) res = response_error_405(route_res.allowed, route_res.allowed_count, &allow_buf, &allow_h);
    else                                  res = response_error_from_status(PARSE_NOT_FOUND);

build_resp: ;
    if (req->request_line.method == HEAD) res.head_only = 1;
    conn->resp_buf.size = (size_t)response_serialize(&res, conn->resp_buf.buffer, conn->resp_buf.cap, conn->keep_alive);

    if (conn->next_req_offset > 0) {
        const size_t pipelined = conn->req_buf.size - conn->next_req_offset;
        memmove(conn->req_buf.buffer, conn->req_buf.buffer + conn->next_req_offset, pipelined);
    }
    else conn->req_buf.size = 0;
    conn->next_req_offset = 0;

    return phase_send_begin(conn);
}


static ConnPhase step_response_send(Connection * conn) {
    const SendReponseStatus status = response_send(conn->fd, &conn->resp_buf, &conn->st.send);

    switch (status) {
        case SEND_HAS_MORE:    return phase_send_begin(conn);
        case SEND_ERROR:       return CONN_CLOSED;
        case SEND_PEER_CLOSED: return CONN_CLOSED;
    case SEND_OK:
        conn->requests++;
        if (!conn->keep_alive || conn->requests >= MAX_REQUESTS) return CONN_CLOSED;
        conn->st.send = (SendSt){0}; // ← reset for next request
        conn->resp_buf.size = 0;
        memset(&conn->req_parsed, 0, sizeof(conn->req_parsed));
        conn->keep_alive = 0; // re-determined per request
        return CONN_READING_REQUEST;
    }
}

KeepAliveStatus connection_step_process(Connection * conn, const Router * router) {
    ConnPhase prev;
    do {
        prev = conn->phase;
        switch (conn->phase) {
            case CONN_READING_REQUEST:      conn->phase = step_header_read(conn);                  break;
            case CONN_READING_BODY_CL:      conn->phase = step_body_cl_read(conn);                 break;
            case CONN_READING_BODY_CHUNKED: conn->phase = step_body_chunked_read(conn);            break;
            case CONN_BUILDING:             conn->phase = step_response_build(conn, router);       break;
            case CONN_SENDING_RESPONSE:     conn->phase = step_response_send(conn);                break;
            case CONN_CLOSED:               /* unreachable */                                      break;
        }
    } while (conn->phase != prev && conn->phase != CONN_CLOSED);
}