//
// Created by redonxharja on 5/3/26.
//

#ifndef HTTP_SERVER_FROM_SCRATCH_HTTPREQUESTLINE_H
#define HTTP_SERVER_FROM_SCRATCH_HTTPREQUESTLINE_H

#include <stddef.h>

#include "ParseResult.h"

#define MAX_METHOD_LEN 8
#define MAX_PATH_LEN 2048
#define MAX_QUERY_LEN 512
#define MAX_REQUEST_LEN (8 * 1024 * 1024)
#define VERSION_LEN 8

typedef enum {
    GET, POST, PUT, DELETE, OPTIONS, HEAD, PATCH, UNKNOWN
} HttpMethod;

HttpMethod http_method_parse(const char *s, size_t len);
char* http_method_show(HttpMethod method);

typedef struct {
    HttpMethod method;
    char path[MAX_PATH_LEN];
    char query[MAX_QUERY_LEN];
    char version[VERSION_LEN * 2];
} HttpRequestLine;

ParseResult method_parse(const char * cur, const char *end, HttpRequestLine * line);
ParseResult uri_parse(const char * cur, const char *end, HttpRequestLine * line);
ParseResult version_parse(const char * cur, const char *end, HttpRequestLine * line);
ParseResult request_line_parse(const char * cur, const char *end, HttpRequestLine * line);
void request_line_show(const HttpRequestLine * line);

#endif //HTTP_SERVER_FROM_SCRATCH_HTTPREQUESTLINE_H