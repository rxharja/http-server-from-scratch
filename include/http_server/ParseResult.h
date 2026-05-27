//
// Created by redonxharja on 5/3/26.
//

#ifndef HTTPSERVER_PARSERESULT_H
#define HTTPSERVER_PARSERESULT_H
#include <stddef.h>
#include <time.h>

#define HTTP_DATE_FMT "%a, %d %b %Y %H:%M:%S GMT"

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
    PARSE_INCOMPLETE,
    PARSE_NOT_FOUND,
    PARSE_NOT_ALLOWED,
    PARSE_SERVER_ERROR
  } ParseStatus;

typedef struct {
    ParseStatus status;
    const char * error_position;
    const char * next;
} ParseResult;


ParseStatus uint_parse(const char *s, size_t len, int base, size_t max, size_t *out);

int digit_value(unsigned char c, int base);

void parse_error_set(ParseResult *res, ParseStatus status, const char * pos);

int http_date_parse(const char *s, time_t *out);

size_t http_date_format(time_t t, char *buf, size_t buf_len);

#endif //HTTPSERVER_PARSERESULT_H