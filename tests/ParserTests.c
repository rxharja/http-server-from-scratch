//
// Created by redonxharja on 4/29/26.
//

#include <stdio.h>
#include <string.h>
#include "../src/HttpRequest.h"

static int total = 0, failed = 0;

static void check(const char *label, const int ok, const char *detail) {
    total++;
    if (ok) {
        printf("ok   %s\n", label);
    } else {
        failed++;
        printf("FAIL %s — %s\n", label, detail);
    }
}

static void expect_method(const char *label, const char *input, const HttpMethod want) {
    HttpRequest r = {0};
    parse_method(input, input + strlen(input), &r);
    check(label, r.method == want, "method mismatch");
}

static void expect_uri(const char *label, const char *input, const ParseHeaderStatus want) {
    HttpRequest r = {0};
    ParseHeaderResult res = parse_uri(input, input + strlen(input), &r);
    check(label, res.status == want, "uri mismatch");
}

static void expect_status(const char *label, const char *input, const ParseHeaderStatus want) {
    HttpRequest r = {0};
    ParseHeaderResult res = parse_header(input, strlen(input), &r);
    check(label, res.status == want, "status mismatch");
}

int main(void) {
    expect_method("Method - plain GET",    "GET /",     GET);
    expect_method("Method - plain POST",   "POST ",     POST);
    expect_method("Method - plain PUT",    "PUT ",      PUT);
    expect_method("Method - plain DELETE", "DELETE ",   DELETE);
    expect_method("Method - Unknown",      "FROBPOO /", UNKNOWN);

    expect_uri("URI - plain Index",       "/index.html ",               PARSE_OK);
    expect_uri("URI - Index with query",  "/index.html?x=1 ",           PARSE_OK);
    expect_uri("URI - Percent Encoding",  "/file%20name ",              PARSE_OK);
    expect_uri("URI - no leading /",      "PUT ",                       PARSE_BAD_REQUEST);
    expect_uri("URI - fail on fragment",  "/index.html# ",              PARSE_BAD_REQUEST);
    expect_uri("URI - Illegal Characters - π",  "/path/π ",              PARSE_BAD_REQUEST);
    expect_uri("URI - Illegal Characters - control",  "/path\t/tab ",    PARSE_BAD_REQUEST);

    expect_status("leading SP",      " GET / HTTP/1.1\r\n\r\n",          PARSE_BAD_REQUEST);
    expect_status("leading HTAB",    "\tGET / HTTP/1.1\r\n\r\n",         PARSE_BAD_REQUEST);
    expect_status("unknown method",  "FROBNICATE / HTTP/1.1\r\n\r\n",    PARSE_BAD_REQUEST);
    expect_status("empty",           "",                                 PARSE_BAD_REQUEST);

    printf("\n%d/%d passed\n", total - failed, total);
    return failed ? 1 : 0;
}
