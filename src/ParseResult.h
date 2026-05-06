//
// Created by redonxharja on 5/3/26.
//

#ifndef HTTPSERVER_PARSERESULT_H
#define HTTPSERVER_PARSERESULT_H
#include <stddef.h>

typedef enum {
    PARSE_OK,
    PARSE_BAD_REQUEST,
    PARSE_URI_TOO_LONG,
    PARSE_HEADER_KEY_TOO_LONG,
    PARSE_HEADER_VALUE_TOO_LONG,
    PARSE_HEADER_TOO_LONG,
    PARSE_VERSION_NOT_SUPPORTED,
    PARSE_PAYLOAD_TOO_LARGE,
    PARSE_NOT_IMPLEMENTED,
    PARSE_INCOMPLETE
  } ParseStatus;

typedef struct {
    ParseStatus status;
    const char * error_position;
    const char * next;
} ParseResult;


ParseStatus parse_uint(const char *s, size_t len, int base, size_t max, size_t *out);

int digit_value(unsigned char c, int base);

void set_parse_error(ParseResult *res, ParseStatus status, const char * pos);

#endif //HTTPSERVER_PARSERESULT_H