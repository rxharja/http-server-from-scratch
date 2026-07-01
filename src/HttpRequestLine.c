//
// Created by redonxharja on 5/3/26.
//

#include "http_server/HttpRequestLine.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "parser.h"

HttpMethod http_method_parse(const char *s, const size_t len) {
    if (memcmp(s, "GET", 3) == 0 && len == 3) return GET;
    if (memcmp(s, "POST", 4) == 0 && len == 4) return POST;
    if (memcmp(s, "PUT", 3) == 0 && len == 3) return PUT;
    if (memcmp(s, "DELETE", 6) == 0 && len == 6) return DELETE;
    return UNKNOWN;
}

char* http_method_show(const HttpMethod method) {
    if (method == GET) return "GET";
    if (method == POST) return "POST";
    if (method == PUT) return "PUT";
    if (method == DELETE) return "DELETE";
    return "UNKNOWN";
}

ParseResult method_parse(const char * cur, const char *end, HttpRequestLine * line) {
    ParseResult res = {0};
    parse_error_set(&res, PARSE_BAD_REQUEST, cur);

    if (cur >= end) return res;
    if (*cur == ' ') return res;
    const char *sp = memchr(cur, ' ', end - cur);
    if (!sp) return res;

    line->method = http_method_parse(cur, sp - cur);
    if (line->method == UNKNOWN) return res;

    res.status = PARSE_OK;
    res.next = sp + 1;
    return res;
}


ParseResult uri_parse(const char * cur, const char *end, HttpRequestLine * line) {
    ParseResult res = {0};
    parse_error_set(&res, PARSE_BAD_REQUEST, cur);

    if (cur >= end) return res;

    const char * sp = memchr(cur, ' ', end - cur);
    if (!sp) return res;

    if (*cur != '/') return res;

    size_t path_len = 0;
    size_t query_len = 0;
    char * buf = line->path;
    size_t * len_ptr = &path_len;
    size_t cap = HTTP_MAX_PATH_LEN;
    int in_query = 0;
    size_t i = 0;
    while (cur + i < sp) {
        // only use this to check for characters
        const char c = cur[i++]; // assigns then increments
        if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7F) return res;
        if (c == '#') return res;

        if (!in_query && c == '?') {
            in_query = 1;
            buf = line->query;
            len_ptr = &query_len;
            cap = HTTP_MAX_QUERY_LEN;
            buf[(*len_ptr)++] = c;
            continue;
        }

        if (*len_ptr >= cap - 1) {
            parse_error_set(&res, PARSE_URI_TOO_LONG, cur + i - 1);
            return res;
        }

        if (c == '%') {
            if (cur + i + 2 > sp) {
                parse_error_set(&res, PARSE_BAD_REQUEST, cur + i - 1);
                return res;
            }

            if (!is_hex((u_char)cur[i]) || !is_hex((u_char)cur[i + 1])) {
                parse_error_set(&res, PARSE_BAD_REQUEST, cur + i - 1);
                return res;
            }

            if (*len_ptr + 3 > cap - 1) {
                parse_error_set(&res, PARSE_URI_TOO_LONG, cur + i - 1);
                return res;
            }

            buf[(*len_ptr)++] = c;
            buf[(*len_ptr)++] = cur[i];
            buf[(*len_ptr)++] = cur[i + 1];
            i += 2;
            continue;
        }

        buf[(*len_ptr)++] = c;
    }

    line->query[query_len] = '\0';
    line->path[path_len] = '\0';
    res.status = PARSE_OK;
    res.next = sp + 1;
    return res;
}

ParseResult version_parse(const char * cur, const char *end, HttpRequestLine * line) {
    ParseResult res = {0};
    parse_error_set(&res, PARSE_BAD_REQUEST, cur);
    if (end - cur != HTTP_VERSION_LEN) return res;
    if (memcmp("HTTP/", cur, 5) != 0) return res;

    const char d1 = cur[5];
    const char period = cur[6];
    const char d2 = end[-1];

    if (!isdigit(d1) || !isdigit(d2) || period != '.') return res;

    const int valid_version = memcmp(&cur[5], "0.9", 3) == 0
                           || memcmp(&cur[5], "1.0", 3) == 0
                           || memcmp(&cur[5], "1.1", 3) == 0;

    if (!valid_version) {
        parse_error_set(&res, PARSE_VERSION_NOT_SUPPORTED, cur);
        return res;
    }

    memcpy(line->version, cur, HTTP_VERSION_LEN);
    line->version[HTTP_VERSION_LEN] = '\0';
    res.status = PARSE_OK;
    res.next = end;
    return res;
}

ParseResult request_line_parse(const char * cur, const char *end, HttpRequestLine * line) {
    const ParseResult method_res = method_parse(cur, end, line);
    if (method_res.status != PARSE_OK) return method_res;

    const ParseResult uri_res = uri_parse(method_res.next, end, line);
    if (uri_res.status != PARSE_OK) return uri_res;

    const ParseResult version_res = version_parse(uri_res.next, end, line);
    return version_res;
}

void request_line_show(const HttpRequestLine * line) {
    printf("%s %s%s %s\n", http_method_show(line->method), line->path, line->query, line->version);
}

