//
// Created by redonxharja on 5/4/26.
//

#include "Connection.h"

#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <asm-generic/errno-base.h>
#include "HttpRequest.h"
#include "ParseResult.h"
#include "../lib/parser.h"

struct addrinfo;

int get_addr_info(struct addrinfo **serv_info, const char * port) {
    struct addrinfo hints = {0};
    int rv;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, port, &hints, serv_info)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int bind_socket(const struct addrinfo * servinfo) {
    const int yes = 1;
    int sockfd = 0;
    const struct addrinfo * p;

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            return EXIT_FAILURE;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        return EXIT_FAILURE;
    }

    return sockfd;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &((struct sockaddr_in*)sa)->sin_addr;
    }

    return &((struct sockaddr_in6*)sa)->sin6_addr;
}

int valid_port(const char * str) {
    char *endptr;
    errno = 0;

    const long num = strtol(str, &endptr, 10);

    if (errno != 0 || *endptr != '\0' || endptr == str) return 1;
    if (num <= 0 || num > 65535) return 1;
    return 0;
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

// TODO: blocking I/O, needs to handle EAGAIN/EWOULDBLOCK
ReadBodyResult recv_body(const int fd, const char *buf, const size_t already_have, const size_t body_len, char * dest_buf) {
    ReadBodyResult res = {0};

    // keep-alive
    if (already_have > body_len) {
        res.status = READ_BODY_OVERREAD;
        res.next_req_offset = body_len;
        res.body_received = body_len;
        memcpy(dest_buf, buf, body_len);
        return res;
    }

    memcpy(dest_buf, buf, already_have); // copy rest of body already received from initial read
    res.body_received = already_have;

    while (1) {
        if (res.body_received == body_len) { res.status = READ_BODY_OK; break; }

        const ssize_t got = recv(fd, &dest_buf[res.body_received], body_len - res.body_received, 0);

        if (got == 0) { res.status = READ_BODY_PEER_CLOSED; break; }

        if (got > 0) res.body_received += got;
        else { // -1
            if (errno == EINTR) continue;
            res.status = READ_BODY_IO_ERROR;
            break;
        }
    }

    return res;
}

ParseResult handle_connection(const int fd) {
    ParseResult req_res = {0};

    HttpRequest *req = malloc(sizeof(HttpRequest));
    char * buf = malloc(8192 * sizeof(char));

    const ReadHeaderResult header_res = recv_header(fd, buf, 8192);
    if (header_res.status == READ_HEADER_PEER_CLOSED) close(fd); // maybe send 400 here
    if (header_res.status == READ_HEADER_IO_ERROR) close(fd);
    if (header_res.status == READ_HEADER_TOO_LARGE) close(fd); // send a 431 here

    const ParseResult parse_req_res = parse_request(buf, header_res.total_received, req);
    if (parse_req_res.status != PARSE_OK) return parse_req_res;

    TransferCoding coding = TE_NONE;
    ParseStatus status = PARSE_OK;
    for (int i = 0; i < req->header_count; i++) {
        if (coding == TE_UNSUPPORTED || status != PARSE_OK) break;
        if (!ascii_ieq(req->headers[i].key, "transfer-encoding")) continue;
        status = parse_transfer_encoding(req->headers[i].value, &coding);
    }

    // parse content-length if no transfer encoding
    if (coding == TE_UNSUPPORTED || status != PARSE_OK) {
        set_parse_error(&req_res, status, buf);
        return req_res;
    }

    // TODO: implement chunking
    if (coding == TE_CHUNKED) {
        set_parse_error(&req_res, PARSE_BAD_REQUEST, buf);
        return req_res;
    }

    const Header * ct_len_h = get_header(req->headers, req->header_count, "content-length");

    // no C-E and no T-E = no body
    if (!ct_len_h) {
        req_res.status = PARSE_OK;
        show_request(req);
        free(req);
        free(buf);
        return req_res;
    }

    size_t body_len = 0;
    req_res.status = parse_uint(ct_len_h->value, strlen(ct_len_h->value), 10, MAX_BODY_LEN, &body_len);
    if (req_res.status != PARSE_OK) return req_res;
    const ReadBodyResult body_res = recv_body(
        fd,
        buf + header_res.body_start,
        header_res.total_received - header_res.body_start,
        body_len,
        req->body);

    if (body_res.status == PARSE_OK) {
        req_res.status = PARSE_OK;
        show_request(req);
        free(req);
        free(buf);
        return req_res;
    }

    free(req);
    free(buf);
    return req_res;
}
