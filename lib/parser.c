//
// Created by redonxharja on 4/27/26.
//

#include "parser.h"

// returns end when no crlf is found
const char *find_crlf(const char *cur, const char *end) {
    while (cur + 1 < end) {
        if (cur[0] == '\r' && cur[1] == '\n') return cur;
        cur++;
    }

    return end;
}

int is_ows(const char *c) {
    return *c == ' ' || *c == '\t';
}

// advances cur past leading SP/HTAB; returns new position.
const char *skip_ows(const char *cur, const char *end) {
    while (cur < end && is_ows(cur)) cur++;
    return cur;
}

// backs up end before SP/HTAB; returns new end.
const char *trim_trailing_ows(const char *start, const char *end) {
    while (start < end && is_ows(&end[-1])) end--;
    return end;
}
