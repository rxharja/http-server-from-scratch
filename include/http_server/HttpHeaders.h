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

const Header * header_find(const Header * headers, size_t header_count, const char * name);
ParseResult header_key_parse(const char *cur, const char *end, Header *header);
ParseResult header_value_parse(const char *cur, const char *end, Header *header);
ParseResult crlf_parse(const char *cur, const char *end);
ParseResult header_line_parse(const char *cur, const char *end, Header * headers, size_t * count);
void headers_show(const Header * headers, size_t header_count);

#endif //HTTPSERVER_HTTPHEADERS_H