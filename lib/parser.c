//
// Created by redonxharja on 4/27/26.
//

#include "parser.h"

#include <ctype.h>

// returns end when no crlf is found
const char *find_crlf(const char *cur, const char *end) {
    while (cur + 1 < end) {
        if (cur[0] == '\r' && cur[1] == '\n') return cur;
        cur++;
    }

    return end;
}

int is_ows(const unsigned char c) {
    return c == ' ' || c == '\t';
}

int is_colon(const unsigned char c) {
    return c == ':';
}


// returns end when no colon is found
const char *find_colon(const char *cur, const char *end) {
    while (cur + 1 < end) {
        if (is_colon((unsigned char)*cur)) return cur;
        cur++;
    }

    return end;
}

// advances cur past leading SP/HTAB; returns new position.
const char *skip_ows(const char *cur, const char *end) {
    while (cur < end && is_ows((unsigned char)*cur)) cur++;
    return cur;
}

// backs up end before SP/HTAB; returns new end.
const char *trim_trailing_ows(const char *start, const char *end) {
    while (start < end && is_ows((unsigned char)end[-1])) end--;
    return end;
}


int is_hex(const unsigned char c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

//tchar          = "!" / "#" / "$" / "%" / "&" / "'" / "*"
//               / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
//               / DIGIT / ALPHA
//               ; any VCHAR, except delimiters
int is_tchar(const unsigned char c) {
    return c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*'
        || c == '+' || c == '-' || c == '.' || c == '^' || c == '_' || c == '`' || c == '|' || c == '~'
        || isalnum(c);
}

//token          = 1*tchar
int is_token(const char * token, const size_t len) {
    if (len <= 0) return 0;

    for (size_t i = 0; i < len; i++) {
        if (!is_tchar(token[i])) return 0;
    }

    return 1;
}

int is_vchar(const unsigned char c) {
    // '!' to '~' including alphanumeric
    return c >= 0x21 && c <= 0x7e;
}

// field-content  = field-vchar
//                  [ 1*( SP / HTAB / field-vchar ) field-vchar ]
// field-vchar    = VCHAR / obs-text
// obs-text       = %x80-FF
int is_field_content_byte(const unsigned char c) {
    return is_ows(c) || is_vchar(c) || c >= 0x80;
}
