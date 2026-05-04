//
// Created by redonxharja on 5/27/25.
//

#include "HttpRequest.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include "../lib/parser.h"

ParseResult parse_header(const char * buf, const size_t len, HttpRequest * req) {
    const char * cur = buf;
    const char * end = trim_trailing_ows(cur, buf + len);

    ParseResult req_line_res = parse_request_line(cur, find_crlf(cur, end), &req->request_line);
    if (req_line_res.status != PARSE_OK) return req_line_res;
    req_line_res = parse_crlf(req_line_res.next, end);
    if (req_line_res.status != PARSE_OK) return req_line_res;
    cur = req_line_res.next;
    ParseResult header_res = {0};

    while (1) {
        // verifying that we find a second crlf in a row
        const ParseResult eol = parse_crlf(cur, end);
        if (eol.status == PARSE_OK) {
            cur = eol.next;
            break;
        }

        header_res = parse_header_line(cur, find_crlf(cur, end), req->headers, &req->header_count);
        if (header_res.status != PARSE_OK) return header_res;
        header_res = parse_crlf(header_res.next, end);
        if (header_res.status != PARSE_OK) return header_res;
        cur = header_res.next;
    }

    header_res.status = PARSE_OK;
    header_res.next = cur;
    return header_res;
}

ParseResult parse_request(const char * buf, const size_t len, HttpRequest * req) {
    ParseResult req_res = {0};
    const ParseResult header_res = parse_header(buf, len, req);
    if (header_res.status != PARSE_OK) return header_res;

    TransferCoding coding = TE_NONE;
    ParseStatus status = PARSE_OK;
    for (int i = 0; i < req->header_count; i++) {
        if (coding == TE_UNSUPPORTED || status != PARSE_OK) break;
        if (!ascii_ieq(req->headers[i].key, "transfer-encoding")) continue;
        status = parse_transfer_encoding(req->headers[i].value, &coding);
    }

    // parse content-length if no transfer encoding
    if (coding == TE_UNSUPPORTED) {
        set_header_error(&req_res, PARSE_BAD_REQUEST, buf);
        return req_res;
    }

    // TODO: implement chunking
    if (coding == TE_CHUNKED) {
        set_header_error(&req_res, PARSE_BAD_REQUEST, buf);
        return req_res;
    }

    const Header * ct_len_h = get_header(req->headers, req->header_count, "content-length");

    // no C-E and no T-E = no body
    if (!ct_len_h) {
        req_res.status = PARSE_OK;
        return req_res;
    }

    size_t body_len = 0;
    req_res.status = parse_content_length(ct_len_h->value, &body_len);
    if (req_res.status != PARSE_OK) return req_res;
    const ParseResult body_res = parse_req_body(header_res.next, body_len, req->body);
    return body_res;
}

static void show_request_line(const HttpRequestLine * line) {
    printf("%s %s%s %s\n", show_http_method(line->method), line->path, line->query, line->version);
}

// TODO: blocking I/O, needs to handle EAGAIN/EWOULDBLOCK
ReadHeaderResult recv_header(const int fd, char *header_buf, const ssize_t header_cap) {
    ReadHeaderResult res = {0};
    while (1) {
        if (res.total_received >= header_cap) {
            res.status = READ_HEADER_TOO_LARGE; // return 431
            break;
        }

        const ssize_t got = recv(fd, &header_buf[res.total_received], header_cap - res.total_received, 0);

        if (got == 0) {
            res.status = READ_HEADER_PEER_CLOSED; // 400
            break;
        }

        if (got > 0) res.total_received += got;
        else { // -1
            if (errno == EINTR) continue;
            res.status = READ_HEADER_IO_ERROR;
            break;
        }

        const char *terminator = memmem(header_buf, res.total_received, "\r\n\r\n", 4);

        if (terminator != NULL) {
            res.status = READ_HEADER_OK;
            res.body_start = terminator - header_buf + 4;
            break;
        }
    }

    return res;
}

void show_request(const HttpRequest * req) {
    printf("\n");
    show_request_line(&req->request_line);
    show_headers(req->headers, req->header_count);
    printf("\n");
    printf("%s\n", req->body);
}
