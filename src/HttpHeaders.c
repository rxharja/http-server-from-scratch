//
// Created by redonxharja on 5/3/26.
//

#include "http_server/HttpHeaders.h"

#include <ctype.h>
#include <stdio.h>

#include "parser.h"

ParseResult header_key_parse(const char * cur, const char * end, Header * header) {
    ParseResult res = {0};
    parse_error_set(&res, PARSE_BAD_REQUEST, cur);

    if (cur >= end || !is_colon((unsigned char)*end)) return res;

    size_t count = 0;
    while (cur < end) {
        if (!is_tchar(*cur)) return res;

        if (count >= HTTP_MAX_HEADER_KEY_LEN - 1) { // account for \0
            parse_error_set(&res, PARSE_HEADER_KEY_TOO_LONG, cur);
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

ParseResult header_value_parse(const char * cur, const char * end, Header * header) {
    ParseResult res = {0};
    parse_error_set(&res, PARSE_BAD_REQUEST, cur);

    if (end - cur > HTTP_MAX_HEADER_VALUE_LEN - 1) { // account for \0
        parse_error_set(&res, PARSE_HEADER_VALUE_TOO_LONG, cur);
        return res;
    }

    if (cur > end) return res; // empty field values are ok as per spec

    cur = ows_skip(cur, end);
    const char * trimmed_end = ows_trim_trailing(cur, end);
    size_t count = 0;
    while (cur < trimmed_end) {
        if (!is_field_content_byte((unsigned char)*cur)) return res;
        header->value[count++] = *cur;
        cur++;
    }

    header->value[count] = '\0';
    res.status = PARSE_OK;
    res.next = end;
    return res;
}

ParseResult header_line_parse(const char * cur, const char * end, Header * headers, size_t * count) {
    ParseResult res = {0};
    if (cur >= end) {
        parse_error_set(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    Header header = {0};
    res = header_key_parse(cur, colon_find(cur, end), &header);
    if (res.status != PARSE_OK) return res;

    res = header_value_parse(res.next, end, &header); // end points to crlf
    if (res.status != PARSE_OK) return res;

    if (*count >= HTTP_MAX_HEADERS) {
        parse_error_set(&res, PARSE_HEADER_TOO_LONG, cur);
        return res;
    }

    headers[(*count)++] = header;
    res.status = PARSE_OK;
    return res;
}

ParseResult crlf_parse(const char * cur, const char * end) {
    ParseResult res = {0};
    if (cur + 2 <= end && is_crlf(cur)) {
        res.status = PARSE_OK;
        res.next = cur + 2;
        return res;
    }

    parse_error_set(&res, PARSE_BAD_REQUEST, cur);
    return res;
}

// name must be null-terminated
const Header * header_find(const Header * headers, const size_t header_count, const char * name) {
    for (int i = 0; i < header_count; i++) {
        const Header *header = &headers[i];
        if (ascii_ieq(header->key, name)) return &headers[i];
    }

    return NULL;
}

void headers_show(const Header * headers, const size_t header_count) {
    for (int i = 0; i < header_count; i++) {
        printf("%s: %s\n", headers[i].key, headers[i].value);
    }
}
