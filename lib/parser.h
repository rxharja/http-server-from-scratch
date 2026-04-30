//
// Created by redonxharja on 4/27/26.
//

#ifndef HTTPSERVER_PARSER_H
#define HTTPSERVER_PARSER_H

const char * find_crlf(const char * cur, const char * end);

// backs up end before SP/HTAB; returns new end.
int is_ows(const char * c);

// advances cur past leading SP/HTAB; returns new position.
const char * skip_ows(const char * cur, const char *end);

// backs up end before SP/HTAB; returns new end.
const char * trim_trailing_ows(const char * start, const char *end);

#endif //HTTPSERVER_PARSER_H
