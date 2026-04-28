//
// Created by redonxharja on 5/27/25.
//

#include <stdio.h>
#include <string.h>
#include "HttpRequest.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "lib/Dictionary.h"

HttpMethod parse_http_method(const char *method) {
    if (strcmp(method, "GET") == 0) return GET;
    if (strcmp(method, "POST") == 0) return POST;
    if (strcmp(method, "PUT") == 0) return PUT;
    if (strcmp(method, "DELETE") == 0) return DELETE;
    return UNKNOWN;
}

char* show_http_method(const HttpMethod method) {
    if (method == GET) return "GET";
    if (method == POST) return "POST";
    if (method == PUT) return "PUT";
    if (method == DELETE) return "DELETE";
    return "UNKNOWN";
}

void parse_request(char * message, HttpRequest * req) {
    const char * line = strtok(message, "\r\n");

    if (line != NULL) {
        char method_str[MAX_METHOD_LEN];
        sscanf(line, "%s %s %s", method_str, req->path, req->version);
        req->method = parse_http_method(method_str);
        line = strtok(NULL, "\r\n");
    }

    req->header_count = 0;

    while (line != NULL && strlen(line) > 0) {
        if (req->header_count >= MAX_HEADERS) break;
        const char * colon = strchr(line, ':');

        if (colon != NULL) {
            size_t key_len = colon - line;
            size_t value_len = strlen(colon + 2);

            strncpy(req->headers[req->header_count].key, line, key_len);
            req->headers[req->header_count].key[key_len] = '\0';

            strncpy(req->headers[req->header_count].value, colon + 2, value_len);
            req->headers[req->header_count].value[value_len] = '\0';

            req->header_count++;
        }

        line = strtok(NULL, "\r\n");
    }
}

void show_request(HttpRequest * req) {
    printf("%s %s %s\r\n", show_http_method(req->method), req->path, req->version);

    for (int i = 0; i < req->header_count; i++) {
        printf("%s: %s\r\n", req->headers[i].key, req->headers[i].value);
    }
}

// todo: blocking I/O, needs to handle EAGAIN/EWOULDBLOCK
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