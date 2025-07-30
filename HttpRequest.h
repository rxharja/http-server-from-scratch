//
// Created by redonxharja on 5/29/25.
//

#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#define MAX_HEADERS 32
#define MAX_METHOD_LEN 8
#define MAX_PATH_LEN 256
#define MAX_VERSION_LEN 16
#define MAX_HEADER_KEY_LEN 64
#define MAX_HEADER_VALUE_LEN 256
#define MAX_REASON_PHRASE_LEN 64

typedef enum HttpMethod {
    GET, POST, PUT, DELETE, OPTIONS, HEAD, PATCH, UNKNOWN
} HttpMethod;

typedef struct Header {
    char key[MAX_HEADER_KEY_LEN];
    char value[MAX_HEADER_VALUE_LEN];
} Header;

typedef struct HttpRequestHeaders {
    HttpMethod method;
    char path[MAX_PATH_LEN];
    char version[MAX_VERSION_LEN];
    Header headers[MAX_HEADERS];
    int header_count;
} HttpRequest;

typedef struct HttpResponse {
    char version[MAX_VERSION_LEN];
    int statusCode;
    char reasonPhrase[MAX_REASON_PHRASE_LEN];
    Header headers[MAX_HEADERS];
    int headerCount;
    char *body;
    long bodyLength;
} HttpResponse;

HttpMethod parse_http_method(const char *method);

char* show_http_method(HttpMethod method);

void parse_request(char * message, HttpRequest * req);

void show_request(HttpRequest * req);

HttpResponse* pack_response(const HttpRequest * req);

void free_response(HttpResponse * res);

void set_header(HttpResponse * res, const char * name, const char * value);

int get_content(const char * path, HttpResponse * res);

char* get_content_type(const char * path);

int serialize_response(const HttpResponse * resp, char * buffer, size_t buffer_size);

#endif //HTTPREQUEST_H
