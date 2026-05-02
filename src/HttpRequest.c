//
// Created by redonxharja on 5/27/25.
//

#include "HttpRequest.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include "../lib/parser.h"

HttpMethod parse_http_method(const char *s, const size_t len) {
    if (memcmp(s, "GET", 3) == 0 && len == 3) return GET;
    if (memcmp(s, "POST", 4) == 0 && len == 4) return POST;
    if (memcmp(s, "PUT", 3) == 0 && len == 3) return PUT;
    if (memcmp(s, "DELETE", 6) == 0 && len == 6) return DELETE;
    return UNKNOWN;
}

char* show_http_method(const HttpMethod method) {
    if (method == GET) return "GET";
    if (method == POST) return "POST";
    if (method == PUT) return "PUT";
    if (method == DELETE) return "DELETE";
    return "UNKNOWN";
}

void set_header_error(ParseHeaderResult *res, ParseHeaderStatus status, const char * pos) {
    res->status = status;
    res->error_position = pos;
}

ParseHeaderResult parse_method(const char * cur, const char *end, HttpRequestLine * line) {
    ParseHeaderResult res = {0};
    if (cur >= end) {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }
    if (*cur == ' ') {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }
    const char *sp = memchr(cur, ' ', end - cur);
    if (!sp) {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }
    line->method = parse_http_method(cur, sp - cur);
    if (line->method == UNKNOWN) {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }
    res.status = PARSE_OK;
    res.next = sp + 1;
    return res;
}


ParseHeaderResult parse_uri(const char * cur, const char *end, HttpRequestLine * line) {
    ParseHeaderResult res = {0};
    if (cur >= end) {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    const char * sp = memchr(cur, ' ', end - cur);
    if (!sp) {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    if (*cur != '/') {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    size_t path_len = 0;
    size_t query_len = 0;
    char * buf = line->path;
    size_t * len_ptr = &path_len;
    size_t cap = MAX_PATH_LEN;
    int in_query = 0;
    size_t i = 0;
    while (cur + i < sp) {
        // only use this to check for characters
        const char c = cur[i++]; // assigns then increments

        if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7F) {
          set_header_error(&res, PARSE_BAD_REQUEST, cur + i - 1);
          return res;
        }

        if (c == '#') {
          set_header_error(&res, PARSE_BAD_REQUEST, cur + i - 1);
          return res;
        }

        if (!in_query && c == '?') {
            in_query = 1;
            buf = line->query;
            len_ptr = &query_len;
            cap = MAX_QUERY_LEN;
            buf[(*len_ptr)++] = c;
            continue;
        }

        if (*len_ptr >= cap - 1) {
            set_header_error(&res, PARSE_URI_TOO_LONG, cur + i - 1);
            return res;
        }

        if (c == '%') {
            if (cur + i + 2 > sp) {
                set_header_error(&res, PARSE_BAD_REQUEST, cur + i - 1);
                return res;
            }

            if (!is_hex((u_char)cur[i]) || !is_hex((u_char)cur[i + 1])) {
                set_header_error(&res, PARSE_BAD_REQUEST, cur + i - 1);
                return res;
            }

            if (*len_ptr + 3 > cap - 1) {
                set_header_error(&res, PARSE_URI_TOO_LONG, cur + i - 1);
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

ParseHeaderResult parse_version(const char * cur, const char *end, HttpRequestLine * line) {
    ParseHeaderResult res = {0};
    if (end - cur != VERSION_LEN) {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    if (memcmp("HTTP/", cur, 5) != 0) {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    const char d1 = cur[5];
    const char period = cur[6];
    const char d2 = end[-1];

    if (!isdigit(d1) || !isdigit(d2) || period != '.') {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    const int valid_version = memcmp(&cur[5], "0.9", 3) == 0
                           || memcmp(&cur[5], "1.0", 3) == 0
                           || memcmp(&cur[5], "1.1", 3) == 0;

    if (!valid_version) {
        set_header_error(&res, PARSE_VERSION_NOT_SUPPORTED, cur);
        return res;
    }

    memcpy(line->version, cur, VERSION_LEN);
    line->version[VERSION_LEN] = '\0';
    res.status = PARSE_OK;
    res.next = end;
    return res;
}

ParseHeaderResult parse_request_line(const char * cur, const char *end, HttpRequestLine * line) {
    const ParseHeaderResult method_res = parse_method(cur, end, line);
    if (method_res.status != PARSE_OK) return method_res;

    const ParseHeaderResult uri_res = parse_uri(method_res.next, end, line);
    if (uri_res.status != PARSE_OK) return uri_res;

    const ParseHeaderResult version_res = parse_version(uri_res.next, end, line);
    return version_res;
}

ParseHeaderResult parse_header_key(const char * cur, const char * end, Header * header) {
    ParseHeaderResult res = {0};

    if (cur >= end || !is_colon((u_char)*end)) {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    size_t count = 0;
    while (cur < end) {
        if (!is_tchar(*cur)) {
            set_header_error(&res, PARSE_BAD_REQUEST, cur);
            return res;
        }

        if (count > MAX_HEADER_KEY_LEN - 1) { // account for \0
            set_header_error(&res, PARSE_HEADER_TOO_LONG, cur);
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

ParseHeaderResult parse_header_value(const char * cur, const char * end, Header * header) {
    ParseHeaderResult res = {0};
    if (end - cur > MAX_HEADER_VALUE_LEN - 1) { // account for \0
        set_header_error(&res, PARSE_HEADER_TOO_LONG, cur);
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
        if (!is_field_content_byte((u_char)*cur)) {
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

ParseHeaderResult parse_header_line(const char * cur, const char * end, HttpRequest * req) {
    ParseHeaderResult res = {0};
    if (cur >= end) {
        set_header_error(&res, PARSE_BAD_REQUEST, cur);
        return res;
    }

    Header header = {0};
    res = parse_header_key(cur, find_colon(cur, end), &header);
    if (res.status != PARSE_OK) return res;

    res = parse_header_value(res.next, end, &header); // end points to crlf
    if (res.status != PARSE_OK) return res;

    if (req->header_count >= MAX_HEADERS) {
        set_header_error(&res, PARSE_HEADER_TOO_LONG, cur);
        return res;
    }

    req->headers[req->header_count++] = header;
    res.status = PARSE_OK;
    return res;
}

ParseHeaderResult parse_header(const char * buf, const size_t len, HttpRequest * req) {
    const char * cur = buf;
    const char * end = trim_trailing_ows(cur, buf + len);
    const ParseHeaderResult req_line_res = parse_request_line(cur, find_crlf(cur, end), &req->request_line);

    if (req_line_res.status != PARSE_OK) return req_line_res;
    assert(*req_line_res.next == '\r' || *req_line_res.next == '\n');
    cur = req_line_res.next; // advance to crlf of first line
    *req_line_res.next == '\r' ? cur += 2 : cur++; // skip over crlf to begin parsing headers

    ParseHeaderResult header_res = {0};
    while (1) {
        if (cur + 2 <= end && memcmp(cur, "\r\n", 2) == 0) {
            cur += 2;
            break;
        }

        header_res = parse_header_line(cur, find_crlf(cur, end), req);
        if (header_res.status != PARSE_OK) return header_res;
        assert(*header_res.next == '\r' || *header_res.next == '\n');
        cur = header_res.next;
        *header_res.next == '\r' ? cur += 2 : cur++;
    }

    header_res.status = PARSE_OK;
    header_res.next = cur;
    return header_res;
}

void show_request_line(const HttpRequestLine * line) {
    printf("%s %s %s\r\n", show_http_method(line->method), line->path, line->version);
}

void show_request(HttpRequest * req) {
    show_request_line(&req->request_line);
    for (int i = 0; i < req->header_count; i++) {
        printf("%s: %s\r\n", req->headers[i].key, req->headers[i].value);
    }
}

// TODO: blocking I/O, needs to handle EAGAIN/EWOULDBLOCK
ReadHeaderResult recv_header(const int fd, char *header_buf, const ssize_t header_cap) {
    ReadHeaderResult res = {0};
    while (1) {
        if (res.total_received >= header_cap) {
            res.status = READ_HEADER_TOO_LARGE; // return 431
            break;
        }

        const ssize_t got = recv(fd, &header_buf[res.total_received], header_cap - res.total_received, 0);

        if (got == 0) {
            res.status = READ_HEADER_PEER_CLOSED; // 400
            break;
        }

        if (got > 0) res.total_received += got;
        else { // -1
            if (errno == EINTR) continue;
            res.status = READ_HEADER_IO_ERROR;
            break;
        }

        const char *terminator = memmem(header_buf, res.total_received, "\r\n\r\n", 4);

        if (terminator != NULL) {
            res.status = READ_HEADER_OK;
            res.body_start = terminator - header_buf + 4;
            break;
        }
    }

    return res;
}
