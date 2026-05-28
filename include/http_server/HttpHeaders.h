//
// Created by redonxharja on 5/3/26.
//

#ifndef HTTPSERVER_HTTPHEADERS_H
#define HTTPSERVER_HTTPHEADERS_H

#include <stddef.h>
#include "ParseResult.h"

#define MAX_HEADERS 32
#define MAX_HEADER_LEN (1024 * 1024 * 8)
#define MAX_HEADER_KEY_LEN 64
#define MAX_HEADER_VALUE_LEN 256

typedef struct {
    char key[MAX_HEADER_KEY_LEN];
    char value[MAX_HEADER_VALUE_LEN];
} Header;

/**
 * Case-insensitive lookup. Returns the first match.
 *
 * @param headers       header array to search
 * @param header_count  number of headers in `headers`
 * @param name          NUL-terminated header name (case-insensitive)
 * @return              pointer into `headers`, or NULL if absent
 */
const Header * header_find(const Header * headers, size_t header_count, const char * name);

/**
 * @param cur     first byte of the header line
 * @param end     one past the last readable byte
 * @param header  output; key populated on PARSE_OK
 */
ParseResult header_key_parse(const char *cur, const char *end, Header *header);

/**
 * @param cur     first byte after the ':' delimiter
 * @param end     one past the last readable byte
 * @param header  output; value populated on PARSE_OK (OWS stripped)
 */
ParseResult header_value_parse(const char *cur, const char *end, Header *header);

/**
 * Consume a bare CRLF (e.g. the empty line ending the header block).
 *
 * @param cur  candidate byte
 * @param end  one past the last readable byte
 */
ParseResult crlf_parse(const char *cur, const char *end);

/**
 * Parse one header line and append it to `headers[*count]`. Bumps `*count`.
 *
 * @param cur      start of the header line
 * @param end      one past the last readable byte
 * @param headers  header array to append into (cap MAX_HEADERS)
 * @param count    in/out current header count
 */
ParseResult header_line_parse(const char *cur, const char *end, Header * headers, size_t * count);

/**
 * @param headers       headers to print to stdout (debug aid)
 * @param header_count  number of headers in `headers`
 */
void headers_show(const Header * headers, size_t header_count);

#endif //HTTPSERVER_HTTPHEADERS_H