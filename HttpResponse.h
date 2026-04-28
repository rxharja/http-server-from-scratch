//
// Created by redonxharja on 4/27/26.
//

#ifndef HTTPSERVER_HEADER_H
#define HTTPSERVER_HEADER_H
#include <stdio.h>
#include "HttpRequest.h"

typedef struct {
    char version[MAX_VERSION_LEN];
    int statusCode;
    char reasonPhrase[MAX_REASON_PHRASE_LEN];
    Header headers[MAX_HEADERS];
    int headerCount;
    Content *content;
} HttpResponse;

HttpResponse* pack_response(const HttpRequest * req, const Dictionary * d);

void free_response(HttpResponse * res);

void set_header(HttpResponse * res, const char * name, const char * value);

int get_content(const char * path, Content * res);

char* get_content_type(const char * path);

int serialize_response(const HttpResponse * resp, char * buffer, size_t buffer_size);

Dictionary * preload_cache(void);

#endif //HTTPSERVER_HEADER_H
