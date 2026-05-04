//
// Created by redonxharja on 5/27/25.
//

#include "HttpRequest.h"

#include <stdio.h>
#include "../lib/parser.h"

ParseResult parse_request(const char * buf, const size_t len, HttpRequest * req) {
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

void show_request(const HttpRequest * req) {
    printf("\n");
    show_request_line(&req->request_line);
    show_headers(req->headers, req->header_count);
    printf("\n");
    printf("%s\n", req->body);
}
