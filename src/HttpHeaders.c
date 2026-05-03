//
// Created by redonxharja on 5/3/26.
//

#include "HttpHeaders.h"

#include <ctype.h>

#include "../lib/parser.h"

ParseResult parse_header_key(const char * cur, const char * end, Header * header) {
    ParseResult res = {0};

    if (cur >= end || !is_colon((unsigned char)*end)) {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    size_t count = 0;
    while (cur < end) {
        if (!is_tchar(*cur)) {
            set_header_error(&res, PARSE_BAD_REQUEST, cur);
            return res;
        }

        if (count >= MAX_HEADER_KEY_LEN - 1) { // account for \0
            set_header_error(&res, PARSE_HEADER_KEY_TOO_LONG, cur);
            return res;
        }

        header->key[count++] = *cur;
        cur++;
    }

    header->key[count] = '\0';
    res.status = PARSE_OK;
    res.next = cur + 1; // skip over ':' where it ends in loop
    return res;
}

ParseResult parse_header_value(const char * cur, const char * end, Header * header) {
    ParseResult res = {0};
    if (end - cur > MAX_HEADER_VALUE_LEN - 1) { // account for \0
        set_header_error(&res, PARSE_HEADER_VALUE_TOO_LONG, cur);
        return res;
    }

    if (cur > end) { // empty field values are ok as per spec
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    cur = skip_ows(cur, end);
    const char * trimmed_end = trim_trailing_ows(cur, end);
    size_t count = 0;
    while (cur < trimmed_end) {
        if (!is_field_content_byte((unsigned char)*cur)) {
            set_header_error(&res, PARSE_BAD_REQUEST, cur);
            return res;
        }

        header->value[count++] = *cur;
        cur++;
    }

    header->value[count] = '\0';
    res.status = PARSE_OK;
    res.next = end;
    return res;
}

ParseResult parse_header_line(const char * cur, const char * end, Header * headers, size_t * count) {
    ParseResult res = {0};
    if (cur >= end) {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    Header header = {0};
    res = parse_header_key(cur, find_colon(cur, end), &header);
    if (res.status != PARSE_OK) return res;

    res = parse_header_value(res.next, end, &header); // end points to crlf
    if (res.status != PARSE_OK) return res;

    if (*count >= MAX_HEADERS) {
        set_header_error(&res, PARSE_HEADER_TOO_LONG, cur);
        return res;
    }

    headers[(*count)++] = header;
    res.status = PARSE_OK;
    return res;
}

ParseResult parse_crlf(const char * cur, const char * end) {
    ParseResult res = {0};
    if (cur + 2 <= end && is_crlf(cur)) {
        res.status = PARSE_OK;
        res.next = cur + 2;
        return res;
    }

    set_header_error(&res, PARSE_BAD_REQUEST, cur);
    return res;
}

// name must be null-terminated
const Header * get_header(const Header * headers, const size_t count, const char * name) {
    for (int i = 0; i < count; i++) {
        const Header *header = &headers[i];
        const char * a = header->key;
        const char * b = name;
        while (*a && *b) {
            if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) break;
            a++; b++;
        }

        if (!*a && !*b)return &headers[i];
    }

    return NULL;
}