//
// Created by redonxharja on 4/27/26.
//

#ifndef HTTPSERVER_PARSER_H
#define HTTPSERVER_PARSER_H
#include <stddef.h>

const char * find_crlf(const char * cur, const char * end);

int is_ows(unsigned char c);

// advances cur past leading SP/HTAB; returns new position.
const char * skip_ows(const char * cur, const char *end);

int is_colon(unsigned char c);

const char *find_colon(const char *cur, const char *end);

// backs up end before SP/HTAB; returns new end.
const char * trim_trailing_ows(const char * start, const char *end);

int is_hex(unsigned char c);

//tchar          = "!" / "#" / "$" / "%" / "&" / "'" / "*"
//               / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
//               / DIGIT / ALPHA
//               ; any VCHAR, except delimiters
int is_tchar(unsigned char c);

//token          = 1*tchar
int is_token(const char * token, size_t len);

int is_vchar(unsigned char c);

// field-content  = field-vchar
//                  [ 1*( SP / HTAB / field-vchar ) field-vchar ]
// field-vchar    = VCHAR / obs-text
// obs-text       = %x80-FF
int is_field_content_byte(unsigned char c);

#endif //HTTPSERVER_PARSER_H
