//
// Created by redonxharja on 5/27/25.
//

#include "http_server/HttpRequest.h"

#include <assert.h>
#include <stdio.h>
#include "parser.h"

ParseResult request_parse(const char * buf, const size_t len, HttpRequest * req) {
    const char * cur = buf;
    const char * end = ows_trim_trailing(cur, buf + len);

    ParseResult req_line_res = request_line_parse(cur, crlf_find(cur, end), &req->request_line);
    if (req_line_res.status != PARSE_OK) return req_line_res;
    req_line_res = crlf_parse(req_line_res.next, end);
    if (req_line_res.status != PARSE_OK) return req_line_res;
    cur = req_line_res.next;
    ParseResult header_res = {0};

    while (1) {
        // verifying that we find a second crlf in a row
        const ParseResult eol = crlf_parse(cur, end);
        if (eol.status == PARSE_OK) {
            cur = eol.next;
            break;
        }

        header_res = header_line_parse(cur, crlf_find(cur, end), req->headers, &req->header_count);
        if (header_res.status != PARSE_OK) return header_res;
        header_res = crlf_parse(header_res.next, end);
        if (header_res.status != PARSE_OK) return header_res;
        cur = header_res.next;
    }

    header_res.status = PARSE_OK;
    header_res.next = cur;
    return header_res;
}

void request_show(const HttpRequest * req) {
    assert(req);
    printf("\n");
    request_line_show(&req->request_line);
    headers_show(req->headers, req->header_count);
    printf("\n");
    if (req->body_len > 0 && req->body) fwrite(req->body, 1, req->body_len, stdout);
}
