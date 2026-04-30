//
// Created by redonxharja on 5/27/25.
//

#include "HttpRequest.h"

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

ParseHeaderResult parse_method(const char * cur, const char *end, HttpRequest * req) {
    ParseHeaderResult res = {0};
    if (cur >= end) {
        res.status = PARSE_BAD_REQUEST;
        res.error_position = cur;
        return res;
    }
    if (*cur == ' ') {
        res.status = PARSE_BAD_REQUEST; // cannot start with whitespace
        res.error_position = cur;
        return res;
    }
    const char *sp = memchr(cur, ' ', end - cur);
    if (!sp) {
        res.status = PARSE_BAD_REQUEST;
        res.error_position = cur;
        return res;
    }
    req->method = parse_http_method(cur, sp - cur);
    if (req->method == UNKNOWN) {
        res.status = PARSE_BAD_REQUEST;
        res.error_position = cur;
        return res;
    }
    res.status = PARSE_OK;
    res.next = sp + 1;
    return res;
}

ParseHeaderResult parse_uri(const char * cur, const char *end, HttpRequest * req) {
    ParseHeaderResult res = {0};
    if (cur >= end) {
        res.status = PARSE_BAD_REQUEST;
        res.error_position = cur;
        return res;
    }

    const char * sp = memchr(cur, ' ', end - cur);
    if (!sp) {
        res.status = PARSE_BAD_REQUEST;
        res.error_position = cur;
        return res;
    }

    if (sp - cur > MAX_PATH_LEN) {
        res.status = PARSE_URI_TOO_LONG;
        res.error_position = cur;
        return res;
    }
    
    if (*cur != '/') {
        res.status = PARSE_BAD_REQUEST;
        res.error_position = cur;
        return res;
    }

    char path_cur = 0;
    while ((cur + path_cur) < sp) {
        if (*(cur + path_cur) < 32 || *(cur + path_cur) > 127) {
          res.status = PARSE_BAD_REQUEST;
          res.error_position = cur + path_cur;
          return res;
        }

        if (*(cur + path_cur) == '#') {
          res.status = PARSE_BAD_REQUEST;
          res.error_position = cur + path_cur;
          return res;
        }

        req->path[path_cur] = *(cur + path_cur);
        path_cur++;
    }
    
    res.status = PARSE_OK;
    res.next = sp + 1;
    return res;
}

ParseHeaderResult parse_version(const char * cur, const char *end, HttpRequest * req) {
    ParseHeaderResult res = {0};
    if (cur >= end) {
        res.status = PARSE_BAD_REQUEST;
        res.error_position = cur;
        return res;
    }
}

ParseHeaderResult parse_request_line(const char * cur, const char *end, HttpRequest * req) {
    const ParseHeaderResult method_res = parse_method(cur, end, req);
    if (method_res.status != PARSE_OK) return method_res;

    const ParseHeaderResult uri_res = parse_uri(method_res.next, end, req);
    if (uri_res.status != PARSE_OK) return uri_res;

    const ParseHeaderResult version_res = parse_version(uri_res.next, end, req);
    return version_res;
}

ParseHeaderResult parse_header(const char * buf, const size_t len, HttpRequest * req) {
    const char * cur = buf;
    const char * end = trim_trailing_ows(cur, buf + len);
    ParseHeaderResult req_line_res = parse_request_line(cur, find_crlf(cur, end), req);
    return req_line_res;
}

// void parse_request(char * message, HttpRequest * req) {
//     const char * line = strtok(message, "\r\n");
//
//     if (line != NULL) {
//         char method_str[MAX_METHOD_LEN];
//         sscanf(line, "%s %s %s", method_str, req->path, req->version);
//         req->method = parse_http_method(method_str);
//         line = strtok(NULL, "\r\n");
//     }
//
//     req->header_count = 0;
//
//     while (line != NULL && strlen(line) > 0) {
//         if (req->header_count >= MAX_HEADERS) break;
//         const char * colon = strchr(line, ':');
//
//         if (colon != NULL) {
//             size_t key_len = colon - line;
//             size_t value_len = strlen(colon + 2);
//
//             strncpy(req->headers[req->header_count].key, line, key_len);
//             req->headers[req->header_count].key[key_len] = '\0';
//
//             strncpy(req->headers[req->header_count].value, colon + 2, value_len);
//             req->headers[req->header_count].value[value_len] = '\0';
//
//             req->header_count++;
//         }
//
//         line = strtok(NULL, "\r\n");
//     }
// }

void show_request(HttpRequest * req) {
    printf("%s %s %s\r\n", show_http_method(req->method), req->path, req->version);

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
