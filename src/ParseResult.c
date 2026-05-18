//
// Created by redonxharja on 5/3/26.
//

#include "ParseResult.h"
#include <ctype.h>
#include <time.h>
#include <string.h>

#include "../lib/parser.h"

void set_parse_error(ParseResult *res, const ParseStatus status, const char * pos) {
    res->status = status;
    res->error_position = pos;
}

int digit_value(const unsigned char c, const int base) {
    int v;
    if (c >= '0' && c <= '9') {
        if (!isdigit(c)) return -1;
        v = c - '0';
    }
    else if (c >= 'a' && c <= 'f') {
        if (!is_hex(c)) return -1;
        v = 10 + (c - 'a');
    }
    else if (c >= 'A' && c <= 'F') {
        if (!is_hex(c)) return -1;
        v = 10 + (c - 'A');
    }
    else return -1;
    return v < base ? v : -1;
}

// Malformed: empty, non-digit, mixed, leading SP/HTAB, trailing SP, +/- signs, 0x10, 1.5
ParseStatus parse_uint(const char *s, const size_t len, const int base, const size_t max, size_t *out) {
    if (len == 0 || *s == '\0') return PARSE_BAD_REQUEST;
    size_t result = 0;

    for (size_t i = 0; i < len; i++) {
        const int digit = digit_value(s[i], base);
        if (digit == -1) return PARSE_BAD_REQUEST;
        if (result > (max - digit) / base) return PARSE_PAYLOAD_TOO_LARGE;
        result = result * base + digit;
    }

    *out = result;
    return PARSE_OK;
}

int parse_http_date(const char *s, time_t *out) {
    struct tm tm = {0};
    if (strptime(s, HTTP_DATE_FMT, &tm) == NULL) return -1;
    *out = timegm(&tm);  // GNU/BSD extension; interprets tm as UTC
    return 0;
}

size_t format_http_date(time_t t, char *buf, const size_t buf_len) {
    struct tm tm;
    gmtime_r(&t, &tm);
    return strftime(buf, buf_len, HTTP_DATE_FMT, &tm);
}