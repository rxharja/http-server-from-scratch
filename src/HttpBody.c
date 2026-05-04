//
// Created by redonxharja on 5/3/26.
//

#include "HttpBody.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "../lib/parser.h"

// Malformed: empty, non-digit, mixed, leading SP/HTAB, trailing SP, +/- signs, 0x10, 1.5
ParseStatus parse_content_length(const char *val, size_t *out) {
    if (*val == '\0') return PARSE_BAD_REQUEST;
    size_t result = 0;
    for (const char *c = val; *c; c++) {
        if (!isdigit((unsigned char)*c)) return PARSE_BAD_REQUEST;
        const size_t digit = *c - '0'; // 0 is 48 (0x30) in ascii
        // result * 10 + digit > MAX_BODY_LEN rearranged
        if (result > (MAX_BODY_LEN - digit) / 10) return PARSE_PAYLOAD_TOO_LARGE;
        result = result * 10 + digit;
    }
    *out = result;
    return PARSE_OK;
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
    if (chunked_found && has_unsupported && chunked_position != total_non_empty - 1) {
        return PARSE_BAD_REQUEST;
    }

    if (has_unsupported) {
        *coding = TE_UNSUPPORTED;
        return PARSE_NOT_IMPLEMENTED;
    }

    *coding = TE_CHUNKED;
    return PARSE_OK;
}

// Caller's contract: `buf` holds at least `len` valid bytes; `body` has room for `len` bytes.
// HTTP bodies are opaque: do NOT assume NUL-termination, do NOT run text-parsers over them.
ParseResult parse_req_body(const char *buf, const size_t len, char *body) {
    ParseResult res = {0};
    memcpy(body, buf, len);
    res.status = PARSE_OK;
    return res;
}