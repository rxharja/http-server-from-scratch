//
// Created by redonxharja on 4/27/26.
//

#ifndef HTTPSERVER_HEADER_H
#define HTTPSERVER_HEADER_H
#define RESPONSE_BUFFER_SIZE (64 * 1024)
#include "HttpRequest.h"
#include "../lib/Dictionary.h"

typedef struct {
    const char *key;
    const char *value;
} ResponseHeader;

typedef struct {
    int status;                    // 200, 404, 500
    const char *reason;            // "OK", "Not Found", etc.
    const ResponseHeader *headers; // caller-owned, usually static
    size_t header_count;
    const char *body;             // body bytes, may be NULL
    size_t body_len;
    int head_only;
} HttpResponse;

typedef HttpResponse (*handler_fn)(const HttpRequest * req);

typedef struct {
    const char *method, *path;
    const handler_fn fn;
} Route;

typedef struct {
    const Route *route;        // NULL if no exact match
    const char *allowed[8];    // methods registered for this path
    size_t      allowed_count; // 0 → 404. >0 with route==NULL → 405.
} RouteLookupResult;

HttpResponse* pack_response(const HttpRequest * req, const Dictionary * d);

ssize_t serialize_response(const HttpResponse * resp, char * buffer, size_t buffer_size, int keep_alive);

RouteLookupResult route_lookup(const Route routes[], size_t count, const char *method, const char *path);

#endif //HTTPSERVER_HEADER_H
