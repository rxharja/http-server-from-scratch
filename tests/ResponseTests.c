//
// Response serialization tests: response_serialize() across the three BodyKind
// variants (BUFFER / NONE / STREAM).
//
// The STREAM case guards the chunked-header path specifically: it must emit
// Transfer-Encoding: chunked, MUST NOT emit Content-Length (the two are mutually
// exclusive), MUST NOT append a body, and MUST NOT write a stray NUL — that last
// one is the sizeof("...")-counts-the-terminator bug. The Date header is dynamic,
// so we assert structurally (substring + framing) rather than byte-for-byte.
//
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "http_server/HttpResponse.h"
#include "test_harness.h"

// Serialize into the caller's buffer and NUL-terminate just past the returned
// length, so the head can be searched with strstr(). Returns the byte count.
static ssize_t do_serialize(char *buf, const size_t cap, const HttpResponse *res, const int keep_alive) {
    const ssize_t n = response_serialize(res, buf, cap, keep_alive);
    if (n >= 0 && (size_t)n < cap) buf[n] = '\0';
    return n;
}

static void want_present(const char *label, const char *buf, const char *needle) {
    check(label, strstr(buf, needle) != NULL, "expected substring missing from head");
}

static void want_absent(const char *label, const char *buf, const char *needle) {
    check(label, strstr(buf, needle) == NULL, "unexpected substring present in head");
}

// The head must terminate with a CRLF blank line at exactly [n-4, n).
static void want_ends_blank_line(const char *label, const char *buf, const ssize_t n) {
    check(label, n >= 4 && memcmp(buf + n - 4, "\r\n\r\n", 4) == 0, "head does not end in CRLFCRLF");
}

// No embedded NUL anywhere in the produced bytes: strlen over the head must equal
// the byte count. A short strlen means a NUL slipped in (the chunked-header bug).
static void want_no_embedded_nul(const char *label, const char *buf, const ssize_t n) {
    check(label, n >= 0 && strlen(buf) == (size_t)n, "embedded NUL in serialized output");
}

void run_response_tests(void) {
    char buf[4096];

    // --- BODY_STREAM: chunked head, no Content-Length, no body, no stray NUL ---
    {
        const HttpResponse res = {
            .status = 200, .reason = "OK",
            .headers = NULL, .header_count = 0,
            .kind = BODY_STREAM,
            .body.stream = { .ctx = NULL, .pull = NULL, .cleanup = NULL },
        };
        const ssize_t n = do_serialize(buf, sizeof(buf), &res, 1);
        check("Stream - serialize ok", n > 0, "serialize returned error");
        want_present("Stream - status line",        buf, "HTTP/1.1 200 OK\r\n");
        want_present("Stream - transfer-encoding",   buf, "Transfer-Encoding: chunked\r\n");
        want_absent ("Stream - no content-length",   buf, "Content-Length");
        want_present("Stream - keep-alive honored",  buf, "Connection: keep-alive\r\n");
        want_ends_blank_line("Stream - ends blank line", buf, n);
        want_no_embedded_nul("Stream - no stray NUL",    buf, n);
    }

    // --- BODY_STREAM + head_only (HEAD request): same head, still no body ---
    {
        const HttpResponse res = {
            .status = 200, .reason = "OK",
            .kind = BODY_STREAM,
            .body.stream = { 0 },
            .head_only = 1,
        };
        const ssize_t n = do_serialize(buf, sizeof(buf), &res, 0);
        check("Stream HEAD - serialize ok", n > 0, "serialize returned error");
        want_present("Stream HEAD - transfer-encoding", buf, "Transfer-Encoding: chunked\r\n");
        want_present("Stream HEAD - close honored",      buf, "Connection: close\r\n");
        want_ends_blank_line("Stream HEAD - ends blank line", buf, n);
        want_no_embedded_nul("Stream HEAD - no stray NUL",    buf, n);
    }

    // --- BODY_BUFFER: Content-Length emitted + body appended after blank line ---
    {
        const HttpResponse res = response_buffer(200, "OK", "hello", 5, NULL, 0);
        const ssize_t n = do_serialize(buf, sizeof(buf), &res, 1);
        check("Buffer - serialize ok", n > 0, "serialize returned error");
        want_present("Buffer - content-length", buf, "Content-Length: 5\r\n");
        want_absent ("Buffer - no chunked",      buf, "Transfer-Encoding");
        // Body sits immediately after the CRLFCRLF blank line.
        check("Buffer - blank line then body",
              n >= 9 && memcmp(buf + n - 9, "\r\n\r\nhello", 9) == 0,
              "blank-line/body framing wrong");
    }

    // --- BODY_BUFFER + head_only: Content-Length kept, body suppressed ---
    {
        HttpResponse res = response_buffer(200, "OK", "hello", 5, NULL, 0);
        res.head_only = 1;
        const ssize_t n = do_serialize(buf, sizeof(buf), &res, 1);
        check("Buffer HEAD - serialize ok", n > 0, "serialize returned error");
        want_present("Buffer HEAD - content-length kept", buf, "Content-Length: 5\r\n");
        want_absent ("Buffer HEAD - body suppressed",     buf, "hello");
        want_ends_blank_line("Buffer HEAD - ends blank line", buf, n);
    }

    // --- BODY_NONE: no Content-Length, no Transfer-Encoding, no body ---
    {
        const HttpResponse res = response_none(204, "No Content", NULL, 0);
        const ssize_t n = do_serialize(buf, sizeof(buf), &res, 1);
        check("None - serialize ok", n > 0, "serialize returned error");
        want_present("None - status line",       buf, "HTTP/1.1 204 No Content\r\n");
        want_absent ("None - no content-length",  buf, "Content-Length");
        want_absent ("None - no chunked",         buf, "Transfer-Encoding");
        want_ends_blank_line("None - ends blank line", buf, n);
        want_no_embedded_nul("None - no stray NUL",    buf, n);
    }
}