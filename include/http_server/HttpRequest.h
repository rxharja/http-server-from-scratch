//
// Created by redonxharja on 5/29/25.
//

#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#include "HttpBody.h"
#include "HttpHeaders.h"
#include "HttpRequestLine.h"
#include "ParseResult.h"
#include "Config.h"

/* HTTP REQUEST */
typedef struct {
    HttpRequestLine request_line;
    Header headers[HTTP_MAX_HEADERS];
    size_t header_count;
    const char * body; // non-owning pointer into a connection-lifetime buffer
    size_t body_len;
} HttpRequest;

/**
 * @param req  request to print to stdout (debug aid)
 */
void request_show(const HttpRequest * req);

/**
 * Parse a full HTTP request from `buf` (request-line + headers; body framing is
 * the caller's job via Content-Length / Transfer-Encoding).
 *
 * @param buf  start of the request bytes
 * @param len  number of bytes readable at `buf`
 * @param req  output; populated on PARSE_OK
 * @return     parse status + cursor position
 */
ParseResult request_parse(const char *buf, size_t len, HttpRequest *req);

#endif //HTTPREQUEST_H
