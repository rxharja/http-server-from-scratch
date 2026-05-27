//
// Created by redonxharja on 4/27/26.
//

#ifndef HTTPSERVER_PARSER_H
#define HTTPSERVER_PARSER_H
#include <stddef.h>

const char * crlf_find(const char * cur, const char * end);

int is_crlf(const char * cur);

int is_ows(unsigned char c);

// advances cur past leading SP/HTAB; returns new position.
const char * ows_skip(const char * cur, const char *end);

int is_colon(unsigned char c);

const char *colon_find(const char *cur, const char *end);

const char *range_trim_to_comma(const char *start, const char *end);

// backs up end before SP/HTAB; returns new end.
const char * ows_trim_trailing(const char * start, const char *end);

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

int ascii_ieq(const char * a, const char * b);

int str_ends_with(const char *str, const char *suffix);

#endif //HTTPSERVER_PARSER_H
