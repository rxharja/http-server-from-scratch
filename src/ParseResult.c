//
// Created by redonxharja on 5/3/26.
//

#include "ParseResult.h"

void set_header_error(ParseResult *res, const ParseStatus status, const char * pos) {
    res->status = status;
    res->error_position = pos;
}