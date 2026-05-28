//
// Created by redonxharja on 5/3/26.
//

#include "http_server/HttpBody.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <asm-generic/errno-base.h>
#include <sys/socket.h>
#include "parser.h"

ParseStatus content_length_parse(const char *val, size_t *out) {
    return uint_parse(val, strlen(val), 10, MAX_BODY_LEN, out);
}

ReadBodyResult conn_recv_body_cl(const int fd, HttpBuffer * req, const size_t body_start, const size_t body_len, CLBodySt * st) {
    assert(req);
    assert(req->buffer);
    assert(st);
    assert(fd >= 0);

    ReadBodyResult res = {0};
    res.status = READ_BODY_HAS_MORE;

    while (1) {
        // body length is not the size of the buffer but the length provided by the header "Content-Length"
        if (st->received >= body_len) {
            res.status = READ_BODY_OK;

            // reading past body_len means we're reading into the start of the next request
            if (st->received > body_len) res.next_req_offset = body_start + body_len;
            break;
        }

        if (req->size >= req->cap) {
            res.status = READ_BODY_TOO_LARGE;
            break;
        }

        const ssize_t got = recv(fd, req->buffer + req->size, req->cap - req->size, 0);

        if (got == 0) {
            res.status = READ_BODY_PEER_CLOSED;
            break;
        }
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // has more, we're coming back here next poll loop
            if (errno == EINTR) continue;
            res.status = READ_BODY_IO_ERROR;
            break;
        }

        req->size += got;
        st->received += got;
    }

    // has more, we're coming back here next poll loop
    return res;
}

ReadBodyResult conn_recv_body_chunked(const int fd, HttpBuffer *req_buf, const size_t body_start, HttpBuffer *dechunked, ChunkedBodySt * st) {
    assert(fd > 0);
    assert(req_buf);
    assert(req_buf->buffer);
    assert(dechunked);
    assert(dechunked->buffer);
    assert(st);
    assert(body_start + st->consumed <= req_buf->size);

    ReadBodyResult res = {0};

    while (1) {
        // drain whatever's already buffered, advancing the decoder until it asks for more
        while (1) {
            const char  *cur    = req_buf->buffer + body_start + st->consumed;
            const size_t in_len = req_buf->size - (body_start + st->consumed);

            const ChunkResult cr = chunk_advance(
                &st->dec, cur, in_len,
                dechunked->buffer + dechunked->size,
                dechunked->cap - dechunked->size);

            // increment consumed by the previous version of the cursor to where next has ended up
            // note this is raw bytes and not decoded bytes.
            if (cr.parse_result.next) st->consumed += (size_t)(cr.parse_result.next - cur);
            dechunked->size += cr.bytes_written;

            if (cr.parse_result.status == PARSE_PAYLOAD_TOO_LARGE) { res.status = READ_BODY_TOO_LARGE; return res; }
            if (cr.parse_result.status != PARSE_OK && cr.parse_result.status != PARSE_INCOMPLETE) {
                res.status = READ_BODY_BAD_DATA;
                return res;
            }

            if (st->dec.phase == CHUNK_DONE) {
                res.status = READ_BODY_OK;
                res.next_req_offset = body_start + st->consumed;
                return res;
            }

            if (cr.parse_result.status == PARSE_INCOMPLETE) break;   // need more wire bytes
            // PARSE_OK but not DONE: more might be buffered — loop and try again
        }

        // PARSE_INCOMPLETE: need to recv more. If the buffer is already full,
        // a single header line is wider than req_buf — surface as 413.
        if (req_buf->size >= req_buf->cap) { res.status = READ_BODY_TOO_LARGE; return res; }

        const ssize_t got = recv(fd, req_buf->buffer + req_buf->size, req_buf->cap - req_buf->size, 0);
        if (got == 0) { res.status = READ_BODY_PEER_CLOSED; return res; }
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { res.status = READ_BODY_HAS_MORE; return res; }
            if (errno == EINTR) continue;   // re-drain (idempotent), then retry recv
            res.status = READ_BODY_IO_ERROR;
            return res;
        }

        req_buf->size += got;
    }
}

ChunkResult chunk_advance(ChunkDecoder * dec, const char * in, const size_t in_len, char * out, const size_t out_avail) {
    assert(dec);
    assert(in);
    assert(out);

    const char * cur = in; // copy the pointer so that we aren't mutating
    ChunkResult res = {0};

    switch (dec->phase) {
        case CHUNK_SIZE: { // parse length and handle ext
            size_t len;

            const char * crlf = crlf_find(cur, cur + in_len); // find \r\n

            // if we didn't find it, or we found it at the very end,
            // and the last two bytes are literally not \r\n we need more bytes from the wire
            const char * end = cur + in_len;
            if (!crlf || (size_t)(end - crlf) < 2) {
                parse_error_set(&res.parse_result, PARSE_INCOMPLETE, cur);
                return res;
            }

            // exts are denoted with a semicolon after the size
            const char *size_end = memchr(cur, ';', crlf - cur);

            // if we don't have the ext, we null-coalesce to where we found the crlf.
            if (!size_end) size_end = crlf;

            // guard against a malformed request \r\n5\r\n\hello\r\n\0\r\n\r\n
            if (size_end == cur) { parse_error_set(&res.parse_result, PARSE_BAD_REQUEST, cur); return res; }

            // then we take that span and convert it to from base16 to 10, storing it on our len variable.
            res.parse_result.status = uint_parse(cur, size_end - cur, 16, MAX_BODY_LEN, &len);
            if (res.parse_result.status != PARSE_OK) return res;

            // a 0-sized chunk is the terminating signal
            if (len == 0) {
                dec->phase = CHUNK_TRAILER;
                res.parse_result.status = PARSE_OK;
                res.parse_result.next = crlf + 2; // advance past 0\r\n
                res.bytes_written = 0;
                return res;
            }

            res.parse_result.status = PARSE_OK;
            res.parse_result.next = crlf + 2; //advance past crlf
            res.bytes_written = 0;
            dec->remaining = len;
            dec->phase = CHUNK_DATA;
            return res;
        }
        case CHUNK_DATA: { // copy the corresponding N bytes of data in the chunk body into our out buffer
            assert(dec->remaining > 0);

            // bound the amount of bytes we take by min(dec->remaining, in_len)
            // it's possible we do not have the full input yet so remaining might be overambitious
            const size_t take = dec->remaining < in_len ? dec->remaining : in_len;
            if (take > out_avail) {
                parse_error_set(&res.parse_result, PARSE_PAYLOAD_TOO_LARGE, cur);
                return res;
            }

            memcpy(out, cur, take); // copy the bytes into out
            dec->remaining -= take; // decrement remaining  by take
            res.bytes_written += take; // increment the bytes written
            res.parse_result.next = cur + take; // advance the cursor
            res.parse_result.status = dec->remaining == 0 ? PARSE_OK : PARSE_INCOMPLETE;
            if (dec->remaining == 0) dec->phase = CHUNK_TRAIL_CR;
            return res;
        }
        case CHUNK_TRAIL_CR: { // ensure we have a \r after our body
            if (in_len == 0) { parse_error_set(&res.parse_result, PARSE_INCOMPLETE, cur); return res; }
            if (*cur != '\r') { parse_error_set(&res.parse_result, PARSE_BAD_REQUEST, cur); return res; }
            dec->phase = CHUNK_TRAIL_LF;
            res.parse_result.next = cur + 1;
            res.parse_result.status = PARSE_OK;
            return res;
        }
        case CHUNK_TRAIL_LF: { // ensure we have a \n after the \r
            if (in_len == 0) { parse_error_set(&res.parse_result, PARSE_INCOMPLETE, cur); return res; }
            if (*cur != '\n') { parse_error_set(&res.parse_result, PARSE_BAD_REQUEST, cur); return res; }
            dec->phase = CHUNK_SIZE;
            res.parse_result.next = cur + 1;
            res.parse_result.status = PARSE_OK;
            return res;
        }
        case CHUNK_TRAILER: { // trailers currently are not supported, current implementation is to reject
            // empty trailer only: expect bare CRLF
            if (in_len < 2) { parse_error_set(&res.parse_result, PARSE_INCOMPLETE, cur); return res; }

            // explicit rejection of trailer fields
            if (cur[0] != '\r' || cur[1] != '\n') {
                parse_error_set(&res.parse_result, PARSE_BAD_REQUEST, cur);
                return res;
            }

            res.parse_result.next = cur + 2;
            res.parse_result.status = PARSE_OK;
            dec->phase = CHUNK_DONE;
            return res;
        }
        case CHUNK_DONE: {
            res.bytes_written = 0;
            res.parse_result.status = PARSE_OK;
            res.parse_result.next = cur;
            return res;
        }
    }

    return res;
}

// Case-insensitive equality of bytes [start, start+len) against the literal "chunked".
// Caller has already validated that the bytes are tchar (so no high-bit / control chars sneak in).
static int eq_chunked(const char *start, const size_t len) {
    if (len != 7) return 0;
    static const char chunked[] = "chunked";
    for (size_t i = 0; i < 7; i++) {
        unsigned char c = (unsigned char)start[i];
        if (c >= 'A' && c <= 'Z') c += 32;   // ASCII tolower
        if (c != (unsigned char)chunked[i]) return 0;
    }
    return 1;
}

// value should be properly stripped of OWS at this point (parse_header_value handles outer OWS).
//
// Two-pass logic: walk the entire list classifying each element, THEN decide. This separates
// "malformed framing" (400) from "well-formed but unsupported coding" (501), instead of
// short-circuiting on the first non-chunked token and conflating the two cases.
//
//   PARSE_OK + TE_CHUNKED  → chunked is the only coding (the supported path).
//   PARSE_NOT_IMPLEMENTED  → list contains non-chunked codings; spec-valid framing
//                            (chunked may be present and last) but we don't decompress.
//   PARSE_BAD_REQUEST      → list is malformed:
//                              - empty value or list of only-empty elements,
//                              - non-token bytes in a coding name,
//                              - chunked appears but isn't last in the list (RFC 9112 §6.1),
//                              - chunked appears more than once.
ParseStatus transfer_encoding_parse(const char *val, TransferCoding *coding) {
    *coding = TE_NONE;
    const char *cur = val;
    const char *end = val + strlen(val);

    int chunked_found    = 0;
    int total_non_empty  = 0;
    int chunked_position = -1;   // index among non-empty elements; -1 if not seen
    int has_unsupported  = 0;

    while (cur < end) {
        const char *tok_start = ows_skip(cur, end);
        while (cur < end && *cur != ',') cur++;
        const char *tok_end = ows_trim_trailing(tok_start, cur);

        if (tok_start < tok_end) {
            // Strip ;-parameters from the coding name (we don't process params for chunked,
            // and any non-chunked coding is rejected anyway).
            const char  *semi     = memchr(tok_start, ';', tok_end - tok_start);
            const char  *name_end = semi ? ows_trim_trailing(tok_start, semi) : tok_end;
            const size_t name_len = (size_t)(name_end - tok_start);

            // Empty coding name (e.g., ";param=value" with no token in front) → malformed.
            if (name_len == 0) return PARSE_BAD_REQUEST;

            // RFC 9110 §5.6.2 — coding name must be a token (1*tchar).
            if (!is_token(tok_start, name_len)) return PARSE_BAD_REQUEST;

            if (eq_chunked(tok_start, name_len)) {
                if (chunked_found) return PARSE_BAD_REQUEST;   // chunked appearing twice
                chunked_found = 1;
                chunked_position = total_non_empty;
            } else {
                has_unsupported = 1;
            }
            total_non_empty++;
        }
        // empty element → skip silently per RFC 9110 §5.6.1

        if (cur < end) cur++;   // skip the comma
    }

    // Empty value or list of nothing-but-empty-elements.
    if (total_non_empty == 0) return PARSE_BAD_REQUEST;

    // RFC 9112 §6.1: when non-chunked codings are stacked, chunked MUST be the final coding.
    // If chunked is present alongside others but isn't the last entry, the framing is malformed.
    if (chunked_found && has_unsupported && chunked_position != total_non_empty - 1) return PARSE_BAD_REQUEST;

    if (has_unsupported) {
        *coding = TE_UNSUPPORTED;
        return PARSE_NOT_IMPLEMENTED;
    }

    *coding = TE_CHUNKED;
    return PARSE_OK;
}