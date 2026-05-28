//
// Created by redonxharja on 4/27/26.
//

#ifndef HTTPSERVER_HEADER_H
#define HTTPSERVER_HEADER_H
#define RESPONSE_BUFFER_SIZE (128 * 1024 * 1024)
#include <stdio.h>
#include "HttpRequest.h"
#include "HttpBuffer.h"
#include "ParseResult.h"

typedef struct {
    const char *key;
    const char *value;
} ResponseHeader;

typedef struct {
    int status;                    // 200, 404, 500
    const char *reason;            // "OK", "Not Found", etc.
    const ResponseHeader *headers; // caller-owned, usually static
    size_t header_count;
    const char *body;             // body bytes, may be NULL
    size_t body_len;
    int head_only;
} HttpResponse;

typedef enum {
    SEND_OK,
    SEND_PEER_CLOSED,
    SEND_HAS_MORE,
    SEND_ERROR
} SendReponseStatus;

// Per-phase state for a response send (partial-write cursor).
typedef struct { size_t sent; } SendSt;

/**
 * Serialize `resp` into the wire format (status line + headers + body) at `buffer`.
 * Adds `Date` and `Connection` headers; respects `resp->head_only` (no body emitted).
 *
 * @param resp         response to serialize
 * @param buffer       output buffer
 * @param buffer_size  capacity of `buffer`
 * @param keep_alive   non-zero to emit `Connection: keep-alive`, zero for `close`
 * @return             bytes written, or negative on overflow / serialization error
 */
ssize_t response_serialize(const HttpResponse * resp, char * buffer, size_t buffer_size, int keep_alive);

/**
 * Drain `resp` to the wire via send(2). Advances `st->sent`. Idempotent across
 * poll wake-ups: re-entering after EAGAIN resumes from the current cursor.
 *
 * @param fd    connection file descriptor (non-blocking, MSG_NOSIGNAL-aware)
 * @param resp  fully-serialized response buffer
 * @param st    in/out per-phase scratch (sent-byte cursor)
 * @return      SEND_OK / SEND_HAS_MORE / SEND_PEER_CLOSED / SEND_ERROR
 */
SendReponseStatus response_send(int fd, const HttpBuffer * resp, SendSt * st);

/**
 * Look up the canned error response for a parse status (400/413/414/431/500/501/505/404).
 *
 * @param s  parse status to map
 * @return   pre-built HttpResponse with status, reason, and short body filled in
 */
HttpResponse response_error_from_status(ParseStatus s);

/**
 * Build a 405 Method Not Allowed response, emitting `Allow: m1, m2, ...` from `allowed`.
 *
 * @param allowed        method strings registered for the path
 * @param allowed_count  number of entries in `allowed`
 * @param allow_buf      scratch buffer the `Allow` header value is composed into
 * @param h              header slot backing the response's headers array
 * @return               405 response referencing `allow_buf`'s contents
 */
HttpResponse response_error_405(const char * const *allowed, size_t allowed_count, const HttpBuffer * allow_buf, ResponseHeader *h);

/**
 * Serialize a canned error response (looked up via response_error_from_status)
 * directly into `resp`, with `Connection: close`.
 *
 * @param resp  destination buffer; its `size` is updated to the serialized length
 * @param s     parse status driving the error response
 */
void response_error_serialize(HttpBuffer * resp, ParseStatus s);

#endif //HTTPSERVER_HEADER_H
