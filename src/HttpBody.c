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

TransferCoding parse_te_token(const char * start, const char * end) {
    const char *semi = memchr(start, ';', end - start);
    if (semi) end = trim_trailing_ows(start, semi);
    if (memcmp(start, "chunked", 7) == 0) return TE_CHUNKED;
    if (start >= end) return TE_NONE;
    return TE_UNSUPPORTED;
}

// value should be properly stripped of OWS at this point
ParseStatus parse_transfer_encoding(const char *val, TransferCoding * coding) {
    *coding = TE_NONE;
    const char *cur = val, *end = val + strlen(val);
    int chunked_found = 0;

    while (cur < end) {
        const char *tok_start = skip_ows(cur, end);
        while (cur < end && *cur != ',') cur++;
        const char *tok_end = trim_trailing_ows(tok_start, cur);

        if (tok_start < tok_end) {
            *coding = parse_te_token(tok_start, tok_end);
            if (*coding == TE_UNSUPPORTED) return PARSE_BAD_REQUEST;
            if (*coding == TE_CHUNKED) {
                if (!chunked_found) chunked_found = 1;
                else { *coding = TE_UNSUPPORTED; return PARSE_BAD_REQUEST; }
            }
        }
        // empty element → skip silently per RFC 9110 §5.6.1
        if (cur < end) cur++;   // skip the comma
    }

    return PARSE_OK;
}

ParseResult parse_req_body(const char * buf, const size_t len, char * body) {
    ParseResult res = {0};
    set_header_error(&res, PARSE_BAD_REQUEST, buf);
    if (strlen(buf) != len) return res;

    for (size_t i = 0; i < len; i++) {
        body[i] = buf[i];
    }

    res.status = PARSE_OK;
    return res;
}