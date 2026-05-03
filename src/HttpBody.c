//
// Created by redonxharja on 5/3/26.
//

#include "HttpBody.h"

#include <ctype.h>

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

// the value should be properly stripped of OWS at this po
ParseStatus parse_transfer_encoding(const char *val, TransferCoding * coding) {
    if (*val == '\0') *coding = TE_NONE;
    return PARSE_OK;
}
