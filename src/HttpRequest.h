//
// Created by redonxharja on 5/29/25.
//

#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#include "HttpBody.h"
#include "HttpHeaders.h"
#include "HttpRequestLine.h"
#include "ParseResult.h"

#define MAX_REASON_PHRASE_LEN 64

/* HTTP REQUEST */
typedef struct {
    HttpRequestLine request_line;
    Header headers[MAX_HEADERS];
    size_t header_count;
    char body[MAX_BODY_LEN];
} HttpRequest;

void show_request(const HttpRequest * req);

ParseResult parse_request(const char *buf, size_t len, HttpRequest *req);

#endif //HTTPREQUEST_H
