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
                               const ParseStatus want_status, const HttpMethod want_method) {
    HttpRequestLine r = {0};
    const ParseResult res = parse_method(input, input + strlen(input), &r);
    check(label, res.status == want_status && r.method == want_method, "method/status mismatch");
}

static void expect_uri(const char *label, const char *input, const ParseStatus want) {
    HttpRequestLine r = {0};
    const ParseResult res = parse_uri(input, input + strlen(input), &r);
    check(label, res.status == want, "uri mismatch");
}

static void expect_version(const char *label, const char *input, const ParseStatus want) {
    HttpRequestLine r = {0};
    // Contract: caller (parse_request_line) hands parse_version the bare
    // version field — CRLF already stripped. Tests mirror that: end = start + len.
    const ParseResult res = parse_version(input, input + strlen(input), &r);
    check(label, res.status == want, "version mismatch");
}

// Same as expect_version but lets the test pin an explicit length, so we can
// pass inputs containing embedded NULs or trailing bytes the parser shouldn't see.
static void expect_version_n(const char *label, const char *input, const size_t len, const ParseStatus want) {
    HttpRequestLine r = {0};
    const ParseResult res = parse_version(input, input + len, &r);
    check(label, res.status == want, "version mismatch");
}

static void expect_uri_with_query(const char *label, const char *input, const char * query, const ParseStatus want) {
    HttpRequestLine r = {0};
    const ParseResult res = parse_uri(input, input + strlen(input), &r);
    check(label, res.status == want, "uri mismatch");
    if (want == PARSE_OK) check(label, strcmp(r.query, query) == 0, "query mismatch");
}

static void expect_uri_with_path(const char *label, const char *input, const char *path, const ParseStatus want) {
    HttpRequestLine r = {0};
    const ParseResult res = parse_uri(input, input + strlen(input), &r);
    check(label, res.status == want, "uri status mismatch");
    check(label, strcmp(r.path, path) == 0, "path mismatch");
}

// Contract: parse_request_line receives [cur, end) with CRLF already stripped
// by the caller. Tests pass bare request lines (no trailing CRLF).
static void expect_request_line(const char *label, const char *input, const ParseStatus want) {
    HttpRequestLine r = {0};
    const ParseResult res = parse_request_line(input, input + strlen(input), &r);
    check(label, res.status == want, "request_line status mismatch");
}

static void expect_status(const char *label, const char *input, const ParseStatus want) {
    HttpRequest r = {0};
    const ParseResult res = parse_header(input, strlen(input), &r);
    check(label, res.status == want, "status mismatch");
}

// Lets a parse_header test pin a length so embedded NULs survive.
static void expect_status_n(const char *label, const char *input, const size_t len,
                            const ParseStatus want) {
    HttpRequest r = {0};
    const ParseResult res = parse_header(input, len, &r);
    check(label, res.status == want, "status mismatch");
}

static void expect_crlf(const char *label, const char *input, const size_t len,
                        const ParseStatus want) {
    const ParseResult res = parse_crlf(input, input + len);
    check(label, res.status == want, "crlf status mismatch");
}

// parse_header_key contract: end points AT the colon (the caller used find_colon).
// Helper finds the colon in the input and passes that as end.
static void expect_header_key(const char *label, const char *input,
                              const char *want_key, const ParseStatus want_status) {
    Header h = {0};
    const char *colon = strchr(input, ':');
    const char *end = colon ? colon : input + strlen(input);
    const ParseResult res = parse_header_key(input, end, &h);
    check(label, res.status == want_status, "header_key status mismatch");
    if (want_status == PARSE_OK && want_key != NULL) {
        check(label, strcmp(h.key, want_key) == 0, "header_key value mismatch");
    }
}

// parse_header_value contract: [cur, end) where end points to CRLF (or buffer end in tests).
// Test input is the bare value (no CRLF).
static void expect_header_value(const char *label, const char *input,
                                const char *want_value, const ParseStatus want_status) {
    Header h = {0};
    const ParseResult res = parse_header_value(input, input + strlen(input), &h);
    check(label, res.status == want_status, "header_value status mismatch");
    if (want_status == PARSE_OK && want_value != NULL) {
        check(label, strcmp(h.value, want_value) == 0, "header_value content mismatch");
    }
}

// Length-pinned variant for inputs containing NUL or other bytes strlen would clip.
static void expect_header_value_n(const char *label, const char *input, const size_t len,
                                  const ParseStatus want_status) {
    Header h = {0};
    const ParseResult res = parse_header_value(input, input + len, &h);
    check(label, res.status == want_status, "header_value status mismatch");
}

// parse_header_line contract: bare line, no CRLF. Asserts the parsed key/value
// landed in req->headers[0] when status is OK.
static void expect_header_line(const char *label, const char *input,
                               const char *want_key, const char *want_value,
                               const ParseStatus want_status) {
    HttpRequest r = {0};
    const ParseResult res = parse_header_line(input, input + strlen(input), r.headers, &r.header_count);
    check(label, res.status == want_status, "header_line status mismatch");
    if (want_status == PARSE_OK) {
        check(label, r.header_count == 1, "header_count != 1 after one parse");
        if (r.header_count >= 1) {
            if (want_key)   check(label, strcmp(r.headers[0].key, want_key) == 0,
                                  "header_line key mismatch");
            if (want_value) check(label, strcmp(r.headers[0].value, want_value) == 0,
                                  "header_line value mismatch");
        }
    }
}

// get_header lookup. want_value == NULL means the helper expects NULL back (not found).
static void expect_get_header(const char *label, const HttpRequest *req,
                              const char *name, const char *want_value) {
    const Header *h = get_header(req->headers, req->header_count, name);
    if (want_value == NULL) {
        check(label, h == NULL, "expected NULL but got a header");
    } else {
        check(label, h != NULL, "expected a header but got NULL");
        if (h != NULL) {
            check(label, strcmp(h->value, want_value) == 0, "value mismatch");
        }
    }
}

// parse_content_length helper.
// Assumed signature: ParseStatus parse_content_length(const char *val, size_t *out);
// On PARSE_OK the parsed length is written to *out; otherwise *out is left zero.
static void expect_content_length(const char *label, const char *input,
                                  const ParseStatus want_status, const size_t want_value) {
    size_t out = 0;
    const ParseStatus got = parse_content_length(input, &out);
    check(label, got == want_status, "content_length status mismatch");
    if (want_status == PARSE_OK) {
        check(label, out == want_value, "content_length value mismatch");
    }
}

// parse_transfer_encoding helper.
// Assumed signature: ParseStatus parse_transfer_encoding(const char *val, TransferCoding *out);
// Where TransferCoding has at least: TE_NONE, TE_CHUNKED, TE_UNSUPPORTED.
//   PARSE_OK + TE_CHUNKED      → list valid, chunked is the only coding.
//   PARSE_OK + TE_UNSUPPORTED  → list valid, contains codings other than chunked
//                                (server should respond 501 at the orchestrator level).
//   PARSE_BAD_REQUEST          → list malformed: chunked not last, bad token, empty value.
static void expect_transfer_encoding(const char *label, const char *input,
                                     const ParseStatus want_status,
                                     const TransferCoding want_te) {
    TransferCoding out = TE_NONE;
    const ParseStatus got = parse_transfer_encoding(input, &out);
    check(label, got == want_status, "transfer_encoding status mismatch");
    if (want_status == PARSE_OK || want_status == PARSE_NOT_IMPLEMENTED) {
        check(label, out == want_te, "transfer_encoding output mismatch");
    }
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

    // parse_crlf — strict CRLF validator with bounds check.
    expect_crlf("CRLF - exact",                "\r\n",     2, PARSE_OK);
    expect_crlf("CRLF - extra trailing bytes", "\r\nfoo",  5, PARSE_OK);
    expect_crlf("CRLF - lone CR (1 byte)",     "\r",       1, PARSE_BAD_REQUEST);
    expect_crlf("CRLF - lone LF (1 byte)",     "\n",       1, PARSE_BAD_REQUEST);
    expect_crlf("CRLF - empty range",          "",         0, PARSE_BAD_REQUEST);
    expect_crlf("CRLF - CR + non-LF",          "\rX",      2, PARSE_BAD_REQUEST);
    expect_crlf("CRLF - LF + non-CR",          "\nX",      2, PARSE_BAD_REQUEST);
    expect_crlf("CRLF - reversed (LF then CR)","\n\r",     2, PARSE_BAD_REQUEST);
    expect_crlf("CRLF - no line endings at all","ab",      2, PARSE_BAD_REQUEST);

    // parse_header_key — token validation, OWS-rejection (via tchar), is_colon at end.
    expect_header_key("HKey - simple",          "Host:",                "Host",          PARSE_OK);
    expect_header_key("HKey - hyphenated",      "Content-Type:",        "Content-Type",  PARSE_OK);
    expect_header_key("HKey - tchar punctuation","X-Hdr_1!#$%&'*+.^`|~:", "X-Hdr_1!#$%&'*+.^`|~", PARSE_OK);
    expect_header_key("HKey - all digits",      "12345:",               "12345",         PARSE_OK);
    expect_header_key("HKey - empty",           ":",                    NULL,            PARSE_BAD_REQUEST);
    expect_header_key("HKey - SP in name",      "Host Name:",           NULL,            PARSE_BAD_REQUEST);
    expect_header_key("HKey - HTAB in name",    "Host\tName:",          NULL,            PARSE_BAD_REQUEST);
    expect_header_key("HKey - SP before colon", "Host :",               NULL,            PARSE_BAD_REQUEST);
    expect_header_key("HKey - paren delim",     "(comment):",           NULL,            PARSE_BAD_REQUEST);
    expect_header_key("HKey - semicolon delim", "X;Y:",                 NULL,            PARSE_BAD_REQUEST);
    expect_header_key("HKey - slash delim",     "X/Y:",                 NULL,            PARSE_BAD_REQUEST);
    expect_header_key("HKey - bracket delim",   "X[Y]:",                NULL,            PARSE_BAD_REQUEST);
    expect_header_key("HKey - DEL byte",        "Host\x7F:",            NULL,            PARSE_BAD_REQUEST);
    expect_header_key("HKey - control byte",    "Ho\x01st:",            NULL,            PARSE_BAD_REQUEST);
    expect_header_key("HKey - no colon at end", "Host",                 NULL,            PARSE_BAD_REQUEST);

    // parse_header_value — OWS trim, content validation, empty allowed (RFC 9110 §5.5).
    expect_header_value("HVal - simple",            "example.com",         "example.com",        PARSE_OK);
    expect_header_value("HVal - empty allowed",     "",                    "",                   PARSE_OK);
    expect_header_value("HVal - leading SP",        "  example.com",       "example.com",        PARSE_OK);
    expect_header_value("HVal - leading HTAB",      "\texample.com",       "example.com",        PARSE_OK);
    expect_header_value("HVal - trailing SP",       "example.com   ",      "example.com",        PARSE_OK);
    expect_header_value("HVal - trailing HTAB",     "example.com\t",       "example.com",        PARSE_OK);
    expect_header_value("HVal - both ends OWS",     "  example.com  ",     "example.com",        PARSE_OK);
    expect_header_value("HVal - only OWS (empty)",  "    ",                "",                   PARSE_OK);
    expect_header_value("HVal - interior SP",       "Mozilla/5.0 (X11)",   "Mozilla/5.0 (X11)",  PARSE_OK);
    expect_header_value("HVal - interior HTAB",     "a\tb",                "a\tb",               PARSE_OK);
    expect_header_value("HVal - obs-text high-bit", "\xC3\xA9" "clair",    "\xC3\xA9" "clair",   PARSE_OK);
    expect_header_value("HVal - punctuation/VCHAR", "/path?q=1&y=2",       "/path?q=1&y=2",      PARSE_OK);

    // Disallowed bytes: control chars, DEL, CR, LF, NUL.
    // Note: closing the string literal terminates the \x hex escape so 'c','d' aren't pulled
    // into the same hex constant. "ab\x01" "cd" concatenates to the same bytes you'd expect.
    expect_header_value("HVal - control 0x01",      "ab\x01" "cd",         NULL, PARSE_BAD_REQUEST);
    expect_header_value("HVal - control 0x1F",      "ab\x1F" "cd",         NULL, PARSE_BAD_REQUEST);
    expect_header_value("HVal - DEL 0x7F",          "ab\x7F" "cd",         NULL, PARSE_BAD_REQUEST);
    expect_header_value_n("HVal - lone CR rejected","ab\rcd",            5,     PARSE_BAD_REQUEST);
    expect_header_value_n("HVal - lone LF rejected","ab\ncd",            5,     PARSE_BAD_REQUEST);
    expect_header_value_n("HVal - embedded NUL",    "ab\0cd",            5,     PARSE_BAD_REQUEST);

    // Length boundaries — buffer is MAX_HEADER_VALUE_LEN = 256 (max storable: 255 + NUL).
    {
        char val_max[256];
        memset(val_max, 'x', 255);
        val_max[255] = '\0';
        expect_header_value("HVal - max length (255)", val_max, val_max, PARSE_OK);
    }
    {
        char val_too_long[257];
        memset(val_too_long, 'x', 256);
        val_too_long[256] = '\0';
        expect_header_value("HVal - too long (256)", val_too_long, NULL, PARSE_HEADER_VALUE_TOO_LONG);
    }

    // parse_header_line — key + colon + value end-to-end (no CRLF).
    expect_header_line("HLine - simple",          "Host: example.com",          "Host",        "example.com",   PARSE_OK);
    expect_header_line("HLine - no SP after col", "Host:example.com",           "Host",        "example.com",   PARSE_OK);
    expect_header_line("HLine - empty value",     "X-Empty:",                   "X-Empty",     "",              PARSE_OK);
    expect_header_line("HLine - empty value+OWS", "X-Empty:   ",                "X-Empty",     "",              PARSE_OK);
    expect_header_line("HLine - extra OWS",       "Host:    example.com   ",    "Host",        "example.com",   PARSE_OK);
    expect_header_line("HLine - tab after colon", "Host:\texample.com",         "Host",        "example.com",   PARSE_OK);
    expect_header_line("HLine - interior SP",     "User-Agent: curl/8.0 dev",   "User-Agent",  "curl/8.0 dev",  PARSE_OK);
    expect_header_line("HLine - no colon",        "Host",                       NULL, NULL,                     PARSE_BAD_REQUEST);
    expect_header_line("HLine - empty key",       ": value",                    NULL, NULL,                     PARSE_BAD_REQUEST);
    expect_header_line("HLine - SP before colon", "Host : value",               NULL, NULL,                     PARSE_BAD_REQUEST);
    expect_header_line("HLine - bad value byte",  "Host: bad\x01value",         NULL, NULL,                     PARSE_BAD_REQUEST);
    expect_header_line("HLine - bad key byte",    "Ho st: value",               NULL, NULL,                     PARSE_BAD_REQUEST);

    // parse_header — strict CRLF + multiple headers + missing terminator cases.
    expect_status("Header - multi-header",                "GET / HTTP/1.1\r\nHost: x\r\nUser-Agent: t\r\n\r\n", PARSE_OK);
    expect_status("Header - no headers, just empty line", "GET / HTTP/1.1\r\n\r\n",                             PARSE_OK);
    expect_status("Header - no CRLF anywhere",            "GET / HTTP/1.1",                                     PARSE_BAD_REQUEST);
    expect_status("Header - no trailing empty line",      "GET / HTTP/1.1\r\nHost: x\r\n",                      PARSE_BAD_REQUEST);
    expect_status("Header - no trailing CRLF at all",     "GET / HTTP/1.1\r\nHost: x",                          PARSE_BAD_REQUEST);
    expect_status("Header - lone LF after req-line",      "GET / HTTP/1.1\nHost: x\r\n\r\n",                    PARSE_BAD_REQUEST);
    expect_status("Header - lone LF after header",        "GET / HTTP/1.1\r\nHost: x\n\r\n",                    PARSE_BAD_REQUEST);
    expect_status_n("Header - embedded NUL in header",    "GET / HTTP/1.1\r\nHost: a\0b\r\n\r\n", 28,           PARSE_BAD_REQUEST);

    // get_header — case-insensitive lookup, prefix-collision, not-found, empty array.
    {
        const HttpRequest r = {
            .headers = {
                { .key = "Host",            .value = "example.com" },
                { .key = "Accept",          .value = "*/*"         },
                { .key = "Accept-Language", .value = "en-US"       },
                { .key = "Content-Type",    .value = "text/html"   },
            },
            .header_count = 4,
        };

        expect_get_header("Get - exact match",              &r, "Host",            "example.com");
        expect_get_header("Get - case lowered",             &r, "host",            "example.com");
        expect_get_header("Get - case raised",              &r, "HOST",            "example.com");
        expect_get_header("Get - mixed case",               &r, "hOsT",            "example.com");
        expect_get_header("Get - last header",              &r, "Content-Type",    "text/html");
        expect_get_header("Get - prefix-collision short",   &r, "Accept",          "*/*");
        expect_get_header("Get - prefix-collision long",    &r, "Accept-Language", "en-US");
        expect_get_header("Get - case-insens long",         &r, "accept-language", "en-US");
        expect_get_header("Get - not found",                &r, "X-Missing",       NULL);
        expect_get_header("Get - prefix-only no match",     &r, "Accept-",         NULL);
        expect_get_header("Get - substring not match",      &r, "Type",            NULL);
        expect_get_header("Get - empty name",               &r, "",                NULL);
    }
    {
        const HttpRequest empty = {0};
        expect_get_header("Get - empty headers array",      &empty, "Host", NULL);
    }

    // parse_content_length — see helper comment for the assumed signature.
    // (You'll need to declare it in HttpRequest.h once the implementation lands.)
    expect_content_length("CL - zero",           "0",       PARSE_OK,           0);
    expect_content_length("CL - small",          "100",     PARSE_OK,           100);
    expect_content_length("CL - larger",         "12345",   PARSE_OK,           12345);
    expect_content_length("CL - empty",          "",        PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - non-digit",      "abc",     PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - mixed",          "12abc",   PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - leading SP",     " 100",    PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - trailing SP",    "100 ",    PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - leading HTAB",   "\t100",   PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - plus sign",      "+100",    PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - negative",       "-100",    PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - hex prefix",     "0x10",    PARSE_BAD_REQUEST,  0);
    expect_content_length("CL - float",          "1.5",     PARSE_BAD_REQUEST,  0);
    // size_t-overflow case — number bigger than any platform's size_t.
    // Should map to PARSE_PAYLOAD_TOO_LARGE (or whatever name you settle on).
    expect_content_length("CL - overflow size_t","999999999999999999999999999",
                                                            PARSE_PAYLOAD_TOO_LARGE, 0);

    // parse_transfer_encoding — see helper comment for assumed signature.
    // Recognition (case-insensitive per RFC 9110 §10.1.4).
    expect_transfer_encoding("TE - chunked",            "chunked",          PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - case Chunked",       "Chunked",          PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - case CHUNKED",       "CHUNKED",          PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - case mixed",         "cHuNkEd",          PARSE_OK,           TE_CHUNKED);

    // Single non-chunked coding → unsupported (server returns 501 at orchestrator).
    expect_transfer_encoding("TE - gzip alone",         "gzip",             PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - deflate alone",      "deflate",          PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - identity alone",     "identity",         PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - compress alone",     "compress",         PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);

    // Chunked last in a stacked list → spec-valid framing, but body content unsupported → 501.
    expect_transfer_encoding("TE - gzip then chunked",  "gzip, chunked",    PARSE_NOT_IMPLEMENTED, TE_UNSUPPORTED);
    expect_transfer_encoding("TE - 2 stacks chunked",   "gzip, deflate, chunked", PARSE_NOT_IMPLEMENTED, TE_UNSUPPORTED);

    // Chunked NOT last → malformed framing per RFC 9112 §6.1 ("MUST apply chunked as the final transfer coding").
    // Spec-strict: this is 400, distinct from "unsupported coding" → 501. If your current parser short-circuits
    // on the first unsupported token regardless of position, these will fail until you scan the whole list
    // before deciding malformed-vs-unsupported.
    expect_transfer_encoding("TE - chunked then gzip",  "chunked, gzip",    PARSE_BAD_REQUEST,  TE_NONE);
    expect_transfer_encoding("TE - chunked mid-list",   "chunked, gzip, deflate", PARSE_BAD_REQUEST, TE_NONE);

    // RFC 9110 §5.6.1 — empty list elements MUST be ignored.
    expect_transfer_encoding("TE - leading comma",      ",chunked",         PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - trailing comma",     "chunked,",         PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - double comma",       "chunked,,",        PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - sparse list",        ", , ,chunked",     PARSE_OK,           TE_CHUNKED);

    // OWS handling around commas.
    expect_transfer_encoding("TE - SP after comma",     "gzip, chunked",    PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - HTAB after comma",   "gzip,\tchunked",   PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - SP before comma",    "gzip ,chunked",    PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);
    expect_transfer_encoding("TE - SP both sides",      "gzip , chunked",   PARSE_NOT_IMPLEMENTED,           TE_UNSUPPORTED);

    // Parameters (chunked carries none in practice, but the grammar allows them — must strip).
    expect_transfer_encoding("TE - chunked w/ params",  "chunked;ext=val",  PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - chunked OWS+params", "chunked ;ext=val", PARSE_OK,           TE_CHUNKED);
    expect_transfer_encoding("TE - gzip w/ params last","gzip;q=0.5, chunked", PARSE_NOT_IMPLEMENTED, TE_UNSUPPORTED);

    // Malformed inputs.
    expect_transfer_encoding("TE - empty value",        "",                 PARSE_BAD_REQUEST,  TE_NONE);
    expect_transfer_encoding("TE - all OWS",            "   ",              PARSE_BAD_REQUEST,  TE_NONE);
    expect_transfer_encoding("TE - only commas",        ",,,",              PARSE_BAD_REQUEST,  TE_NONE);
    expect_transfer_encoding("TE - bad token paren",    "(invalid)",        PARSE_BAD_REQUEST,  TE_NONE);
    expect_transfer_encoding("TE - bad token slash",    "weird/coding",     PARSE_BAD_REQUEST,  TE_NONE);

    const char * req_body =
        "GET /about?x=y HTTP/1.1\r\n"
        "Host: www.example.com\r\n"
        "Accept-Language: en-US\r\n"
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "Hello, World!";

    HttpRequest req = {0};
    parse_request(req_body, 200, &req);
    show_request(&req);

    printf("\n%d/%d passed\n", total - failed, total);
    return failed ? 1 : 0;
}
