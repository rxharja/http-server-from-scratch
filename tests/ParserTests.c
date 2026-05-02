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
    HttpRequestLine r = {0};
    parse_method(input, input + strlen(input), &r);
    check(label, r.method == want, "method mismatch");
}

// Asserts both the status (OK/BAD/...) and the resolved method enum.
// Useful where the existing expect_method only inspects r.method.
static void expect_method_full(const char *label, const char *input,
                               const ParseHeaderStatus want_status, const HttpMethod want_method) {
    HttpRequestLine r = {0};
    const ParseHeaderResult res = parse_method(input, input + strlen(input), &r);
    check(label, res.status == want_status && r.method == want_method, "method/status mismatch");
}

static void expect_uri(const char *label, const char *input, const ParseHeaderStatus want) {
    HttpRequestLine r = {0};
    const ParseHeaderResult res = parse_uri(input, input + strlen(input), &r);
    check(label, res.status == want, "uri mismatch");
}

static void expect_version(const char *label, const char *input, const ParseHeaderStatus want) {
    HttpRequestLine r = {0};
    // Contract: caller (parse_request_line) hands parse_version the bare
    // version field — CRLF already stripped. Tests mirror that: end = start + len.
    const ParseHeaderResult res = parse_version(input, input + strlen(input), &r);
    check(label, res.status == want, "version mismatch");
}

// Same as expect_version but lets the test pin an explicit length, so we can
// pass inputs containing embedded NULs or trailing bytes the parser shouldn't see.
static void expect_version_n(const char *label, const char *input, const size_t len, const ParseHeaderStatus want) {
    HttpRequestLine r = {0};
    const ParseHeaderResult res = parse_version(input, input + len, &r);
    check(label, res.status == want, "version mismatch");
}

static void expect_uri_with_query(const char *label, const char *input, const char * query, const ParseHeaderStatus want) {
    HttpRequestLine r = {0};
    const ParseHeaderResult res = parse_uri(input, input + strlen(input), &r);
    check(label, res.status == want, "uri mismatch");
    if (want == PARSE_OK) check(label, strcmp(r.query, query) == 0, "query mismatch");
}

static void expect_uri_with_path(const char *label, const char *input, const char *path, const ParseHeaderStatus want) {
    HttpRequestLine r = {0};
    const ParseHeaderResult res = parse_uri(input, input + strlen(input), &r);
    check(label, res.status == want, "uri status mismatch");
    check(label, strcmp(r.path, path) == 0, "path mismatch");
}

// Contract: parse_request_line receives [cur, end) with CRLF already stripped
// by the caller. Tests pass bare request lines (no trailing CRLF).
static void expect_request_line(const char *label, const char *input, const ParseHeaderStatus want) {
    HttpRequestLine r = {0};
    const ParseHeaderResult res = parse_request_line(input, input + strlen(input), &r);
    check(label, res.status == want, "request_line status mismatch");
}

static void expect_status(const char *label, const char *input, const ParseHeaderStatus want) {
    HttpRequest r = {0};
    const ParseHeaderResult res = parse_header(input, strlen(input), &r);
    check(label, res.status == want, "status mismatch");
}

int main(void) {
    expect_method("Method - plain GET",    "GET /",     GET);
    expect_method("Method - plain POST",   "POST ",     POST);
    expect_method("Method - plain PUT",    "PUT ",      PUT);
    expect_method("Method - plain DELETE", "DELETE ",   DELETE);
    expect_method("Method - Unknown",      "FROBPOO /", UNKNOWN);

    // Status + method together.
    expect_method_full("Method - GET status",        "GET /",     PARSE_OK,          GET);
    expect_method_full("Method - unknown is BAD",    "FROBPOO /", PARSE_BAD_REQUEST, UNKNOWN);
    expect_method_full("Method - empty input",       "",          PARSE_BAD_REQUEST, GET); // GET == 0 from zero-init
    expect_method_full("Method - leading SP",        " GET /",    PARSE_BAD_REQUEST, GET);
    expect_method_full("Method - no SP at all",      "GET",       PARSE_BAD_REQUEST, GET);
    expect_method_full("Method - lowercase",         "get /",     PARSE_BAD_REQUEST, UNKNOWN);
    expect_method_full("Method - mixed case",        "Get /",     PARSE_BAD_REQUEST, UNKNOWN);
    expect_method_full("Method - GET prefix only",   "GETT /",    PARSE_BAD_REQUEST, UNKNOWN);
    expect_method_full("Method - GET prefix long",   "GETPOST /", PARSE_BAD_REQUEST, UNKNOWN);
    expect_method_full("Method - lone SP",           " ",         PARSE_BAD_REQUEST, GET);

    expect_uri("URI - plain Index",       "/index.html ",               PARSE_OK);
    expect_uri_with_query("URI - With Query",  "/index.html?x=1 ", "?x=1", PARSE_OK);

    expect_uri_with_query("URI - With 2 Queries",  "/index.html?x=1?y=2 ", "?x=1?y=2", PARSE_OK);
    expect_uri_with_query("URI - Query with Fragment","/index.html?x=1#y=2 ","",PARSE_BAD_REQUEST);
    expect_uri("URI - Percent Encoding",  "/file%20name ",              PARSE_OK);
    expect_uri("URI - no leading /",      "PUT ",                       PARSE_BAD_REQUEST);
    expect_uri("URI - fail on fragment",  "/index.html# ",              PARSE_BAD_REQUEST);
    expect_uri("URI - Illegal Characters - π",  "/path/π ",              PARSE_BAD_REQUEST);
    expect_uri("URI - Illegal Characters - control",  "/path\t/tab ",    PARSE_BAD_REQUEST);

    expect_uri_with_path("URI - root only",        "/ ",            "/",                PARSE_OK);
    expect_uri_with_path("URI - trailing slash",   "/foo/ ",        "/foo/",            PARSE_OK);
    expect_uri_with_path("URI - sub-delims pass",  "/a-_.~!$&'()*+,;=:@ ", "/a-_.~!$&'()*+,;=:@", PARSE_OK);
    expect_uri_with_query("URI - empty query",     "/?",            "?",                PARSE_BAD_REQUEST); // no SP delimiter
    expect_uri_with_query("URI - empty query SP",  "/? ",           "?",                PARSE_OK);
    expect_uri_with_query("URI - query no path",   "/?x=1 ",        "?x=1",             PARSE_OK);

    // Percent-encoding edge cases.
    expect_uri("URI - percent at end no hex",   "/% ",          PARSE_BAD_REQUEST);
    expect_uri("URI - percent one hex only",    "/%2 ",         PARSE_BAD_REQUEST);
    expect_uri("URI - percent bad second hex",  "/%2G ",        PARSE_BAD_REQUEST);
    expect_uri("URI - percent bad first hex",   "/%G0 ",        PARSE_BAD_REQUEST);
    expect_uri("URI - percent uppercase hex",   "/%2A%2F ",     PARSE_OK);
    expect_uri("URI - percent lowercase hex",   "/%2a%2f ",     PARSE_OK);
    expect_uri_with_path("URI - percent preserved literal", "/%20foo ", "/%20foo", PARSE_OK);

    // No SP delimiter at all → caller would never strip CRLF, but the parser
    // requires SP to terminate the URI field, so this is BAD.
    expect_uri("URI - no SP delimiter",         "/index.html",   PARSE_BAD_REQUEST);

    // Boundary: the very first byte must be '/'.
    expect_uri("URI - starts with query mark",  "?x=1 ",         PARSE_BAD_REQUEST);
    expect_uri("URI - starts with letter",      "index ",        PARSE_BAD_REQUEST);

    // Well-formed: exactly "HTTP/X.Y", 8 bytes, single DIGIT each side.
    expect_version("Version - HTTP/1.1",         "HTTP/1.1", PARSE_OK);
    expect_version("Version - HTTP/1.0",         "HTTP/1.0", PARSE_OK);
    expect_version("Version - HTTP/0.9",         "HTTP/0.9", PARSE_OK);
    expect_version("Version - HTTP/2.0",         "HTTP/2.0", PARSE_VERSION_NOT_SUPPORTED);

    // Length must be exactly 8.
    expect_version("Version - too short",        "HTTP/1.",  PARSE_BAD_REQUEST);
    expect_version("Version - empty",            "",         PARSE_BAD_REQUEST);
    expect_version("Version - trailing junk",    "HTTP/1.1x",PARSE_BAD_REQUEST);
    expect_version("Version - multi-digit major","HTTP/10.1",PARSE_BAD_REQUEST);
    expect_version("Version - multi-digit minor","HTTP/1.10",PARSE_BAD_REQUEST);

    // "HTTP" is case-sensitive.
    expect_version("Version - lowercase name",   "http/1.1", PARSE_BAD_REQUEST);
    expect_version("Version - mixed case name",  "Http/1.1", PARSE_BAD_REQUEST);

    // Wrong protocol name.
    expect_version("Version - HTTPS not allowed","HTTPS/1.1",PARSE_BAD_REQUEST); // length 8 but wrong literal
    expect_version("Version - HTP misspelling",  "HTP/1.11", PARSE_BAD_REQUEST); // length 8 but wrong literal

    // Structural separators in the right places.
    expect_version("Version - missing slash",    "HTTP-1.1", PARSE_BAD_REQUEST);
    expect_version("Version - missing dot",      "HTTP/1-1", PARSE_BAD_REQUEST);
    expect_version("Version - extra slash",      "HTTP//1.1",PARSE_BAD_REQUEST); // length 9, but also tests slash count if length check is loose
    expect_version_n("Version - extra slash @8", "HTTP//1.", 8, PARSE_BAD_REQUEST);

    // No internal whitespace.
    expect_version("Version - SP after HTTP",    "HTTP /1.", PARSE_BAD_REQUEST);
    expect_version("Version - SP after slash",   "HTTP/ 1.1",PARSE_BAD_REQUEST);
    expect_version("Version - HTAB inside",      "HTTP/1\t1",PARSE_BAD_REQUEST);

    // Non-DIGIT in major/minor positions.
    expect_version("Version - alpha major",      "HTTP/A.1", PARSE_BAD_REQUEST);
    expect_version("Version - alpha minor",      "HTTP/1.A", PARSE_BAD_REQUEST);
    expect_version("Version - punct major",      "HTTP/-.1", PARSE_BAD_REQUEST);

    // Embedded NUL must not short-circuit a memcmp-style check.
    expect_version_n("Version - embedded NUL",   "HTTP/\0.1", 8, PARSE_BAD_REQUEST);

    // parse_request_line — bare line, no CRLF (caller strips it).
    expect_request_line("ReqLine - GET / HTTP/1.1",  "GET / HTTP/1.1",                PARSE_OK);
    expect_request_line("ReqLine - POST w/ query",   "POST /api?x=1 HTTP/1.0",        PARSE_OK);
    expect_request_line("ReqLine - DELETE",          "DELETE /resource/42 HTTP/1.1",  PARSE_OK);
    expect_request_line("ReqLine - missing version", "GET /",                         PARSE_BAD_REQUEST);
    expect_request_line("ReqLine - missing URI",     "GET HTTP/1.1",                  PARSE_BAD_REQUEST);
    expect_request_line("ReqLine - missing method",  " / HTTP/1.1",                   PARSE_BAD_REQUEST);
    expect_request_line("ReqLine - extra trailing",  "GET / HTTP/1.1 ",               PARSE_BAD_REQUEST);
    expect_request_line("ReqLine - double SP",       "GET  / HTTP/1.1",               PARSE_BAD_REQUEST);
    expect_request_line("ReqLine - empty",           "",                              PARSE_BAD_REQUEST);
    expect_request_line("ReqLine - lowercase ver",   "GET / http/1.1",                PARSE_BAD_REQUEST);

    // parse_header — full buffer including CRLF terminator(s).
    expect_status("Header - valid GET",     "GET / HTTP/1.1\r\n\r\n",                 PARSE_OK);
    expect_status("Header - valid POST",    "POST /api?x=1 HTTP/1.0\r\n\r\n",         PARSE_OK);
    expect_status("Header - bad version",   "GET / HTTP/1.10\r\n\r\n",                PARSE_BAD_REQUEST);
    expect_status("Header - bad URI",       "GET /pa#th HTTP/1.1\r\n\r\n",            PARSE_BAD_REQUEST);
    expect_status("leading SP",      " GET / HTTP/1.1\r\n\r\n",          PARSE_BAD_REQUEST);
    expect_status("leading HTAB",    "\tGET / HTTP/1.1\r\n\r\n",         PARSE_BAD_REQUEST);
    expect_status("unknown method",  "FROBNICATE / HTTP/1.1\r\n\r\n",    PARSE_BAD_REQUEST);
    expect_status("empty",           "",                                 PARSE_BAD_REQUEST);

    printf("\n%d/%d passed\n", total - failed, total);
    return failed ? 1 : 0;
}
