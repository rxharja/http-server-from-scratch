//
// Created by RedonXharja on 5/8/2026.
//

#ifndef HTTPSERVER_HTTPSERVER_H
#define HTTPSERVER_HTTPSERVER_H
#include "HttpResponse.h"

#define MAX_REQUESTS 100

typedef struct {
    size_t len;
    const char * content_type;
    char data[];
} CachedFile;

typedef Dictionary ContentCache;

/*
 * Known HTTP/1.1 compliance gaps:
 *   - Expect: 100-continue not handled; clients may stall before sending large bodies
 *   - Chunked Transfer-Encoding only supported in requests, not responses
 *   - Absolute-form request targets (GET http://host/path) not parsed; affects proxy use
 *   - Chunked trailer fields not supported (RFC 9112 §7.1.2)
 */
int run_server(const char * port, const Route routes[], size_t count, size_t backlog);

ContentCache * content_cache_create();

int cache_static_dir(ContentCache * cache, const char * dir_path, const char * url_prefix);

int cache_file(ContentCache * cache, const char * url_path, CachedFile * file);

void content_cache_free(ContentCache * cache);

#endif //HTTPSERVER_HTTPSERVER_H
