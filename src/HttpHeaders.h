//
// Created by redonxharja on 5/3/26.
//

#ifndef HTTPSERVER_HTTPHEADERS_H
#define HTTPSERVER_HTTPHEADERS_H

#include <stddef.h>
#include "ParseResult.h"

#define MAX_HEADERS 32
#define MAX_HEADER_KEY_LEN 64
#define MAX_HEADER_VALUE_LEN 256

typedef struct {
    char key[MAX_HEADER_KEY_LEN];
    char value[MAX_HEADER_VALUE_LEN];
} Header;

const Header * get_header(const Header * headers, size_t header_count, const char * name);
ParseResult parse_header_key(const char *cur, const char *end, Header *header);
ParseResult parse_header_value(const char *cur, const char *end, Header *header);
ParseResult parse_crlf(const char *cur, const char *end);
ParseResult parse_header_line(const char *cur, const char *end, Header * headers, size_t * count);
void show_headers(const Header * headers, size_t header_count);

#endif //HTTPSERVER_HTTPHEADERS_H