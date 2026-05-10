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
#include "HttpResponse.h"
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
ReadHeaderResult recv_header(const int fd, char *header_buf, const size_t already_have, const ssize_t header_cap) {
    ReadHeaderResult res = {0};
    res.total_received = already_have;
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

ReadBodyResult recv_chunked_body(const int fd, char *buf, size_t have, const size_t buf_cap, char * dest_buf) {
    ReadBodyResult res = {0};
    while (1) {
        const ChunkResult dechunk_res = body_dechunk(buf, buf + have, dest_buf);

        if (dechunk_res.parse_result.status == PARSE_BAD_REQUEST) {
            res.status = READ_BODY_BAD_DATA;
            return res;
        }

        res.body_received = dechunk_res.chunk_size;

        if (dechunk_res.parse_result.status == PARSE_OK) {
            res.status = READ_BODY_OK;
            return res;
        }

        if (have >= buf_cap) {
            res.status = READ_BODY_TOO_LARGE;
            return res;
        }

        const ssize_t got = recv(fd, buf+have, buf_cap - have, 0);

        if (got == 0) { res.status = READ_BODY_PEER_CLOSED; break; }
        if (got < 0) { // -1
            if (errno == EINTR) continue;
            res.status = READ_BODY_IO_ERROR;
            break;
        }
        have += got;
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
static HttpResponse to_http_response(const ParseStatus s) {
    static const HttpResponse r400 = { .status = 400, .reason = "Bad Request",
                                       .body = "Bad Request", .body_len = 11 };
    static const HttpResponse r413 = { .status = 413, .reason = "Payload Too Large",
                                       .body = "Payload too large", .body_len = 17 };
    static const HttpResponse r414 = { .status = 414, .reason = "URI Too Long",
                                       .body = "URI Too Long", .body_len = 12 };
    static const HttpResponse r431 = { .status = 431, .reason = "Request Header Fields Too Large",
                                       .body = "Request Header Fields Too Large", .body_len = 31 };
    static const HttpResponse r500 = { .status = 500, .reason = "Internal Server Error",
                                       .body = "Internal Server Error", .body_len = 21 };
    static const HttpResponse r501 = { .status = 501, .reason = "Not Implemented",
                                       .body = "Not Implemented", .body_len = 15 };
    static const HttpResponse r505 = { .status = 505, .reason = "HTTP Version Not Supported",
                                       .body = "HTTP Version Not Supported", .body_len = 26 };
    static const HttpResponse r404 = { .status = 404, .reason = "Not Found",
                                       .body = "Not Found", .body_len = 9 };

    switch (s) {
        case PARSE_BAD_REQUEST:           return r400;
        case PARSE_PAYLOAD_TOO_LARGE:     return r413;
        case PARSE_URI_TOO_LONG:          return r414;
        case PARSE_HEADER_KEY_TOO_LONG:
        case PARSE_HEADER_VALUE_TOO_LONG:
        case PARSE_HEADER_TOO_LONG:       return r431;
        case PARSE_NOT_IMPLEMENTED:       return r501;
        case PARSE_VERSION_NOT_SUPPORTED: return r505;
        case PARSE_NOT_FOUND:             return r404;
        default:                          return r500;
    }
}

HttpResponse synthesize_405(const char * const *allowed, const size_t allowed_count, char *allow_buf, const size_t allow_buf_size, ResponseHeader *h) {
    for (size_t i = 0; i < allowed_count; i++) {
        if (i > 0) strncat(allow_buf, ", ", allow_buf_size - strlen(allow_buf) - 1);
        strncat(allow_buf, allowed[i], allow_buf_size - strlen(allow_buf) - 1);
    }
    h->key = "Allow";
    h->value = allow_buf;
    return (HttpResponse) {
        .status = 405,                .reason = "Method Not Allowed",
        .body = "Method Not Allowed", .body_len = sizeof("Method Not Allowed"),
        .headers = h,                 .header_count = 1
    };
}

KeepAliveStatus handle_connection(const int fd, const Route routes[], const size_t route_count, char *out_buf, const size_t out_buf_size, const size_t already_have) {
    KeepAliveStatus status = {0};
    HttpResponse res = to_http_response(PARSE_BAD_REQUEST);
    char res_allow_buf[128] = {0};
    ResponseHeader allow_h = {0};

    HttpRequest *req = malloc(sizeof(HttpRequest));
    char *req_buf = malloc(MAX_BODY_LEN * sizeof(char));

    const ReadHeaderResult header_res = recv_header(fd, req_buf, already_have, MAX_HEADER_LEN);
    if (header_res.status != READ_HEADER_OK) {
        res = to_http_response(header_res.status == READ_HEADER_TOO_LARGE
            ? PARSE_HEADER_TOO_LONG : PARSE_BAD_REQUEST);
        goto cleanup;
    }

    const ParseResult parse_req_res = parse_request(req_buf, header_res.total_received, req);
    if (parse_req_res.status != PARSE_OK) {
        res = to_http_response(parse_req_res.status);
        goto cleanup;
    }

    // HTTP/1.1 requires having a host header
    if (!ascii_ieq(req->request_line.version, "http/1.0")) {
        Header * host_header = get_header(req->headers, req->header_count, "host");
        if (!host_header) { res = to_http_response(PARSE_BAD_REQUEST); goto cleanup; }
    }

    // Keep-Alive or close decision, next req offset set.
    Header * ka_header = get_header(req->headers, req->header_count, "connection");
    if (!ka_header || ascii_ieq("keep-alive", ka_header->value)) status.keep_alive = 1;

    TransferCoding coding = TE_NONE;
    ParseStatus te_status = PARSE_OK;
    for (int i = 0; i < req->header_count; i++) {
        if (coding == TE_UNSUPPORTED || te_status != PARSE_OK) break;
        if (!ascii_ieq(req->headers[i].key, "transfer-encoding")) continue;
        te_status = parse_transfer_encoding(req->headers[i].value, &coding);
    }

    if (coding == TE_UNSUPPORTED) { res = to_http_response(PARSE_NOT_IMPLEMENTED); goto cleanup; }
    if (te_status != PARSE_OK)    { res = to_http_response(te_status);               goto cleanup; }

    ReadBodyResult body_res = {0};
    if (coding == TE_CHUNKED) {
        body_res = recv_chunked_body(
            fd,
            req_buf + header_res.body_start,
            header_res.total_received - header_res.body_start,
            MAX_BODY_LEN - header_res.body_start,
            req->body);
        if (body_res.status != READ_BODY_OK) { res = to_http_response(PARSE_BAD_REQUEST); goto cleanup; }
        req->body_len = body_res.body_received;
    }
    else {
        const Header *ct_len_h = get_header(req->headers, req->header_count, "content-length");
        if (!ct_len_h) req->body_len = 0;
        else {
            size_t body_len = 0;
            const ParseStatus ps = parse_uint(ct_len_h->value, strlen(ct_len_h->value), 10, MAX_BODY_LEN, &body_len);
            if (ps != PARSE_OK) { res = to_http_response(ps); goto cleanup; }
            body_res = recv_body(fd,
                req_buf + header_res.body_start,
                header_res.total_received - header_res.body_start,
                body_len, req->body);
            if (body_res.status != READ_BODY_OK) { res = to_http_response(PARSE_BAD_REQUEST); goto cleanup; }
            req->body_len = body_res.body_received;
        }
    }
    status.next_req_offset = body_res.next_req_offset;

    const HttpMethod method = req->request_line.method == HEAD ? GET : req->request_line.method;
    const RouteLookupResult route_res = route_lookup(routes, route_count, show_http_method(method), req->request_line.path);

    if (route_res.route)                  res = route_res.route->fn(req);
    else if (route_res.allowed_count > 0) res = synthesize_405(route_res.allowed, route_res.allowed_count, res_allow_buf, sizeof(res_allow_buf), &allow_h);
    else                                  res = to_http_response(PARSE_NOT_FOUND);

    if (req->request_line.method == HEAD) res.head_only = 1;

    show_request(req);

cleanup: ;
    status.bytes_to_send = serialize_response(&res, out_buf, out_buf_size, status.keep_alive);
    free(req);
    free(req_buf);
    return status;
}
