//
// Created by redonxharja on 5/3/26.
//

#include "HttpBody.h"

#include <string.h>

#include "../lib/parser.h"

ParseStatus parse_content_length(const char *val, size_t *out) {
    return parse_uint(val, strlen(val), 10, MAX_BODY_LEN, out);
}

ChunkResult parse_chunk(const char * buf, const char * end, char * dest) {
    ChunkResult res = {0};
    const char * cur = buf;

    // parse length and handle ext
    size_t len;
    const char * crlf = find_crlf(cur, end);
    if (!crlf || (crlf == end && end[0] != '\r' && end[1] != '\n')) {
        set_parse_error(&res.parse_result, PARSE_INCOMPLETE, cur);
        return res;
    }
    const char *size_end = memchr(cur, ';', crlf - cur);
    if (!size_end) size_end = crlf;
    res.parse_result.status = parse_uint(cur, size_end - cur, 16, MAX_BODY_LEN, &len);
    if (res.parse_result.status != PARSE_OK) return res;

    cur = crlf + 2; //advance past crlf
    if (len == 0) {
        res.parse_result.status = PARSE_OK;
        res.parse_result.next = cur;
        res.chunk_size = 0;
        return res;
    }

    // parse content
    if (cur + len + 2 > end) {
        set_parse_error(&res.parse_result, PARSE_INCOMPLETE, cur);
        return res;
    }
    if (cur[len] != '\r' || cur[len + 1] != '\n' ) {
        set_parse_error(&res.parse_result, PARSE_BAD_REQUEST, cur);
        return res;
    }

    memcpy(dest, cur, len);
    res.parse_result.status = PARSE_OK;
    if (cur + len + 2 <= end) res.parse_result.next = cur + len + 2;
    res.chunk_size = len;
    return res;
}

ParseResult body_dechunk(const char * buf, const char * end, char * dest) {
    const char * cur = buf;
    size_t total = 0;
    ChunkResult c = {0};
    while (1) {
        c = parse_chunk(cur, end, dest + total);
        if (c.parse_result.status != PARSE_OK) return c.parse_result;
        cur = c.parse_result.next;
        if (c.chunk_size == 0) break;
        total += c.chunk_size;
    }

    while (1) {
        if (cur + 2 > end) {
            set_parse_error(&c.parse_result, PARSE_INCOMPLETE, cur);
            return c.parse_result;
        }

        if (cur[0] == '\r' && cur[1] == '\n') { cur += 2; break; }   // body terminator
        const char *crlf = find_crlf(cur, end);
        if (!crlf) {
            set_parse_error(&c.parse_result, PARSE_INCOMPLETE, cur);
            return c.parse_result;
        }
        cur = crlf + 2;
    }

    return (ParseResult){ .next = cur, .status = PARSE_OK };
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
ParseStatus parse_transfer_encoding(const char *val, TransferCoding *coding) {
    *coding = TE_NONE;
    const char *cur = val;
    const char *end = val + strlen(val);

    int chunked_found    = 0;
    int total_non_empty  = 0;
    int chunked_position = -1;   // index among non-empty elements; -1 if not seen
    int has_unsupported  = 0;

    while (cur < end) {
        const char *tok_start = skip_ows(cur, end);
        while (cur < end && *cur != ',') cur++;
        const char *tok_end = trim_trailing_ows(tok_start, cur);

        if (tok_start < tok_end) {
            // Strip ;-parameters from the coding name (we don't process params for chunked,
            // and any non-chunked coding is rejected anyway).
            const char  *semi     = memchr(tok_start, ';', tok_end - tok_start);
            const char  *name_end = semi ? trim_trailing_ows(tok_start, semi) : tok_end;
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