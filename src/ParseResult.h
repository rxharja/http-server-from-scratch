//
// Created by redonxharja on 5/3/26.
//

#ifndef HTTPSERVER_PARSERESULT_H
#define HTTPSERVER_PARSERESULT_H

typedef enum {
    PARSE_OK,
    PARSE_BAD_REQUEST,
    PARSE_URI_TOO_LONG,
    PARSE_HEADER_KEY_TOO_LONG,
    PARSE_HEADER_VALUE_TOO_LONG,
    PARSE_HEADER_TOO_LONG,
    PARSE_VERSION_NOT_SUPPORTED,
    PARSE_PAYLOAD_TOO_LARGE,
    PARSE
  } ParseStatus;

typedef struct {
    ParseStatus status;
    const char * error_position;
    const char * next;
} ParseResult;

void set_header_error(ParseResult *res, ParseStatus status, const char * pos);

#endif //HTTPSERVER_PARSERESULT_H