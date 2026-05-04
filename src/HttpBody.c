//
// Created by redonxharja on 5/3/26.
//

#include "HttpBody.h"

#include <ctype.h>
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
    if (ascii_ieq(start, "chunked")) return TE_CHUNKED;
    if (start == end) return TE_NONE;
    return TE_UNSUPPORTED;
}

// value should be properly stripped of OWS at this point
ParseStatus parse_transfer_encoding(const char *val, TransferCoding * coding) {
    if (*val == '\0') *coding = TE_NONE;
    const char *cur = val, *end = val + strlen(val);
    int chunked_found = 0;

    while (cur < end) {
        cur = skip_ows(cur, end);
        const char *tok_start = cur;
        while (cur < end && *cur != ',') cur++;
        const char *tok_end = trim_trailing_ows(tok_start, cur);

        if (tok_start < tok_end) {
            *coding = parse_te_token(tok_start, tok_end);
            if (*coding == TE_UNSUPPORTED) return PARSE_BAD_REQUEST;
            if (*coding == TE_CHUNKED) {
                if (!chunked_found) chunked_found = 1;
                else return PARSE_BAD_REQUEST;
            }
        }
        // empty element → skip silently per RFC 9110 §5.6.1
        if (cur < end) cur++;   // skip the comma
    }

    return PARSE_OK;
}

ParseResult parse_req_body(const char * buf, const size_t len, char * body) {
    ParseResult res = {0};
    res.status = PARSE_BAD_REQUEST;
    res.error_position = buf;
    return res;
}

