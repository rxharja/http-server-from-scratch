//
// Created by redonxharja on 5/4/26.
//

#include "Connection.h"

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <asm-generic/errno-base.h>
#include <sys/socket.h>

#include "http_server/HttpBody.h"
#include "http_server/HttpRequest.h"
#include "http_server/HttpResponse.h"
#include "http_server/ParseResult.h"
#include "parser.h"
#include "http_server/HttpServer.h"

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

static ConnPhase phase_send_begin(Connection *conn) {
    conn->st.send = (SendSt){ .sent = 0 };
    return CONN_SENDING_RESPONSE;
}

static ConnPhase phase_send_100_begin(Connection *conn) {
    conn->st.send = (SendSt){ .sent = 0 };
    return CONN_SENDING_100;
}

static ConnPhase phase_body_dispatch(Connection * conn) {
    // body that is Chunked
    if (conn->body_coding == TE_CHUNKED) {
        conn->st.chunked = (ChunkedBodySt) { .dec = {0}, };
        conn->body_dechunked.size = 0;
        return CONN_READING_BODY_CHUNKED;
    }

    // request without a body
    if (conn->body_len == 0) {
        conn->req_parsed.body_len = 0;
        return CONN_BUILDING;
    }

    // body with content-length
    assert(conn->body_len > 0);
    assert((size_t)conn->body_start <= conn->req_buf.size);

    conn->st.cl = (CLBodySt) { .received = conn->req_buf.size - conn->body_start };

    return CONN_READING_BODY_CL;
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

    conn->body_start = header_res.body_start;

    const int is_http_1_0 = ascii_ieq(conn->req_parsed.request_line.version, "http/1.0");

    if (!is_http_1_0) {
        // host required after HTTP/1.0
        if (!header_find(conn->req_parsed.headers, conn->req_parsed.header_count, "host")) {
            response_error_serialize(&conn->resp_buf, PARSE_BAD_REQUEST);
            return phase_send_begin(conn);
        }

        // default to keep alive if we don't have a connection header
        const Header * ka_header = header_find(conn->req_parsed.headers, conn->req_parsed.header_count, "connection");
        if (!ka_header || ascii_ieq("keep-alive", ka_header->value)) conn->keep_alive = 1;
    }

    conn->body_coding = TE_NONE;
    ParseStatus te_status = PARSE_OK;
    for (int i = 0; i < conn->req_parsed.header_count; i++) {
        if (conn->body_coding == TE_UNSUPPORTED || te_status != PARSE_OK) break;
        if (!ascii_ieq(conn->req_parsed.headers[i].key, "transfer-encoding")) continue;
        te_status = transfer_encoding_parse(conn->req_parsed.headers[i].value, &conn->body_coding);
    }

    if (conn->body_coding == TE_UNSUPPORTED) {
        response_error_serialize(&conn->resp_buf, PARSE_NOT_IMPLEMENTED);
        return phase_send_begin(conn);
    }

    if (te_status != PARSE_OK) {
        response_error_serialize(&conn->resp_buf, te_status);
        return phase_send_begin(conn);
    }

    // // RFC 6.1, chunked is HTTP/1.1+ only
    if (is_http_1_0 && conn->body_coding == TE_CHUNKED) {
        response_error_serialize(&conn->resp_buf, PARSE_BAD_REQUEST);
        return phase_send_begin(conn);
    }

    const Header *ct_len_h = header_find(conn->req_parsed.headers, conn->req_parsed.header_count, "content-length");

    // prevent smuggling by returning 400
    if (ct_len_h && conn->body_coding == TE_CHUNKED) {
        response_error_serialize(&conn->resp_buf, PARSE_BAD_REQUEST);
        return phase_send_begin(conn);
    }

    if (ct_len_h) {
        const ParseStatus ps = uint_parse(ct_len_h->value, strlen(ct_len_h->value), 10, MAX_BODY_LEN, &conn->body_len);
        if (ps != PARSE_OK) {
            response_error_serialize(&conn->resp_buf, ps);
            return phase_send_begin(conn);
        }
    }

    const int has_body = conn->body_coding == TE_CHUNKED        // Transfer-Encoding: chunked
                      || (ct_len_h && conn->body_len > 0);      // Content-Length: N

    const int body_buffered = conn->req_buf.size > conn->body_start; // body already buffered

    // Per RFC: clients MUST NOT send content with HEAD.
    if (conn->req_parsed.request_line.method == HEAD && has_body) {
        response_error_serialize(&conn->resp_buf, PARSE_BAD_REQUEST);
        return phase_send_begin(conn);
    }

    // expect should only matter for clients greater than HTTP/1.0
    if (is_http_1_0) return phase_body_dispatch(conn);

    const Header * expect = header_find(conn->req_parsed.headers, conn->req_parsed.header_count, "expect");

    // if we don't have a body, or it's buffered, or we don't find the expect header, or the header isn't 100-continue
    if (!has_body || body_buffered || !expect || !ascii_ieq("100-continue", expect->value)) {
        return phase_body_dispatch(conn);
    }

    return phase_send_100_begin(conn);
}

#define CONTINUE_100_BYTES "HTTP/1.1 100 Continue\r\n\r\n"

static ConnPhase step_send_100(Connection * conn) {
    static const char continue_100[] = CONTINUE_100_BYTES;

    static HttpBuffer continue_100_buf = {
        .buffer = (char*)continue_100, .size = sizeof(continue_100) - 1, .cap = sizeof(continue_100) - 1,
    };

    const SendReponseStatus status = response_send(conn->fd, &continue_100_buf, &conn->st.send);

    switch (status) {
        case SEND_HAS_MORE: return CONN_SENDING_100;
        case SEND_OK: return phase_body_dispatch(conn);
        case SEND_PEER_CLOSED:
        case SEND_ERROR:
        default:
            return CONN_CLOSED;
    }
}

static ConnPhase step_body_cl_read(Connection * conn) {
    assert(conn);
    assert(conn->phase == CONN_READING_BODY_CL);

    const ReadBodyResult body_res = conn_recv_body_cl(conn->fd, &conn->req_buf, conn->body_start, conn->body_len, &conn->st.cl);

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

    const ReadBodyResult body_res = conn_recv_body_chunked(conn->fd, &conn->req_buf, conn->body_start, &conn->body_dechunked, &conn->st.chunked);

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
        const CachedFile * sf = dict_find(router->registry, req->request_line.path);
        if (sf) { res = response_cached(&conn->req_parsed, sf); goto build_resp; }

        const ContentLookupResult d = cache_dynamic_lookup(router->dynamic_cache, req, req->request_line.path);

        switch (d.status) {
            case CONTENT_MISS:                                       break;
            case CONTENT_GONE:         res = response_error_from_status(PARSE_NOT_FOUND); goto build_resp;
            case CONTENT_NOT_MODIFIED: res = response_dynamic_304(d.entry);                 goto build_resp;
            case CONTENT_HIT:          res = response_dynamic(d.entry); goto build_resp;
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
        conn->body_start = 0;
        conn->body_len = 0;
        conn->body_coding = TE_NONE;
        return CONN_READING_REQUEST;
    }
}

KeepAliveStatus connection_step_process(Connection * conn, const Router * router) {
    ConnPhase prev;
    do {
        prev = conn->phase;
        switch (conn->phase) {
            case CONN_READING_REQUEST:      conn->phase = step_header_read(conn);                  break;
            case CONN_SENDING_100:          conn->phase = step_send_100(conn);                     break;
            case CONN_READING_BODY_CL:      conn->phase = step_body_cl_read(conn);                 break;
            case CONN_READING_BODY_CHUNKED: conn->phase = step_body_chunked_read(conn);            break;
            case CONN_BUILDING:             conn->phase = step_response_build(conn, router);       break;
            case CONN_SENDING_RESPONSE:     conn->phase = step_response_send(conn);                break;
            case CONN_CLOSED:               /* unreachable */                                      break;
        }
    } while (conn->phase != prev && conn->phase != CONN_CLOSED);
}