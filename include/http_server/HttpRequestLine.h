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

/**
 * @param s    method token bytes (e.g. "GET")
 * @param len  number of bytes to consider
 * @return     resolved HttpMethod, or UNKNOWN if the token doesn't match a known verb
 */
HttpMethod http_method_parse(const char *s, size_t len);

/**
 * @param method  enum value to render
 * @return        static, NUL-terminated method string (e.g. "GET"); never NULL
 */
char* http_method_show(HttpMethod method);

typedef struct {
    HttpMethod method;
    char path[MAX_PATH_LEN];
    char query[MAX_QUERY_LEN];
    char version[VERSION_LEN * 2];
} HttpRequestLine;

/**
 * @param cur   first byte of the method field
 * @param end   one past the last readable byte
 * @param line  output; method set on PARSE_OK
 */
ParseResult method_parse(const char * cur, const char *end, HttpRequestLine * line);

/**
 * Parses the request-target (path + optional query). Rejects fragments.
 *
 * @param cur   first byte of the URI field
 * @param end   one past the last readable byte
 * @param line  output; path and query populated on PARSE_OK
 */
ParseResult uri_parse(const char * cur, const char *end, HttpRequestLine * line);

/**
 * @param cur   first byte of the HTTP-version field
 * @param end   one past the last readable byte
 * @param line  output; version populated on PARSE_OK
 */
ParseResult version_parse(const char * cur, const char *end, HttpRequestLine * line);

/**
 * Parses the full request-line: method SP request-target SP HTTP-version CRLF.
 *
 * @param cur   start of the request-line
 * @param end   one past the last readable byte
 * @param line  output; all fields populated on PARSE_OK
 */
ParseResult request_line_parse(const char * cur, const char *end, HttpRequestLine * line);

/**
 * @param line  request line to print to stdout (debug aid)
 */
void request_line_show(const HttpRequestLine * line);

#endif //HTTP_SERVER_FROM_SCRATCH_HTTPREQUESTLINE_H