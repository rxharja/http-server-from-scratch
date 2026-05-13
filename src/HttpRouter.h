//
// Created by RedonXharja on 5/13/2026.
//

#ifndef HTTPSERVER_HTTPROUTER_H
#define HTTPSERVER_HTTPROUTER_H
#include "HttpRequest.h"
#include "HttpResponse.h"

typedef HttpResponse (*handler_fn)(const HttpRequest * req);
typedef HttpResponse (*handler_fn_with_data)(const HttpRequest * req, const void * data);

typedef union {
    handler_fn fn;
    handler_fn_with_data fn_with_data;
} handler;

typedef struct {
    const char *method, *path;
    const handler handler;
    const void * data;
} Route;

typedef struct {
    size_t len;
    const char * content_type;
    char data[];
} CachedFile;

typedef Dictionary ContentCache;

typedef struct {
    const Route * routes;
    const size_t route_count;
    ContentCache * static_files;
} Router;

typedef struct {
    const Route *route;        // NULL if no exact match
    const char *allowed[8];    // methods registered for this path
    size_t      allowed_count; // 0 → 404. >0 with route==NULL → 405.
} RouteLookupResult;

RouteLookupResult route_lookup(const Route routes[], size_t count, const char *method, const char *path);

ContentCache * content_cache_create();

int cache_static_dir(ContentCache * cache, const char * dir_path, const char * url_prefix);

int cache_file(ContentCache * cache, const char * url_path, CachedFile * file);

void content_cache_free(ContentCache * cache);

char* get_content_type(const char * path);

HttpResponse from_cached_file(const HttpRequest * req, const CachedFile * file);


#endif //HTTPSERVER_HTTPROUTER_H
