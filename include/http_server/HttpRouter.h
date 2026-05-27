//
// Created by RedonXharja on 5/13/2026.
//

#ifndef HTTPSERVER_HTTPROUTER_H
#define HTTPSERVER_HTTPROUTER_H
#include <time.h>
#include <sys/types.h>
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Dictionary.h"

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
    ResponseHeader headers[3];
    char data[];
} CachedFile;

typedef struct {
    size_t len;
    time_t mtime;
    off_t size;                // for stat comparison alongside mtime
    char fs_path[256];         // source path, for revalidation re-reads
    char etag[40];             // W/"<mtime>-<size>"
    char last_modified[32];    // RFC 7231 IMF-fixdate
    char len_str[24];          // backs content-length header
    ResponseHeader headers[5]; // [0] content-type [1] content-length [2] ETag [3] last-modified [4] cache-control
    char data[];               // file bytes
} DynamicCachedFile;

typedef enum {
    DYN_NOT_REGISTERED, // url not in cache -> fall through to routes
    DYN_GONE,           // url registered but stat() failed -> 404
    DYN_NOT_MODIFIED,   // client's validators match -> 304
    DYN_HIT             // serve full body and invalidate cache
} DynamicLookupStatus;

typedef struct {
    DynamicLookupStatus status;
    const DynamicCachedFile * file; // valid for DYN_NOT_MODIFIED / DYN_HIT
} DynamicLookupResult;

typedef struct {
    const Route *route;        // NULL if no exact match
    const char *allowed[8];    // methods registered for this path
    size_t      allowed_count; // 0 → 404. >0 with route==NULL → 405.
} RouteLookupResult;

typedef Dictionary ContentCache;

typedef struct {
    const Route * routes;
    const size_t route_count;
    ContentCache * static_cache; // immutable, no revalidation
    ContentCache * dynamic_cache; // re-stats files on every hit.
} Router;

RouteLookupResult route_lookup(const Route routes[], size_t count, const char *method, const char *path);

ContentCache * content_cache_create();

int static_dir_cache(ContentCache * cache, const char * dir_path, const char * url_prefix);

int cache_file(ContentCache * cache, const char * url_path, CachedFile * file);

void content_cache_free(ContentCache * cache);

char* content_type_get(const char * path);

HttpResponse response_cached(const HttpRequest * req, const CachedFile * file);

DynamicLookupResult cache_dynamic_lookup(ContentCache *cache, const HttpRequest *req, const char *url_path);

HttpResponse response_dynamic_304(const DynamicCachedFile *f);

HttpResponse response_dynamic(const DynamicCachedFile *f);

int router_has_duplicate_routes(const Router * router);

#endif //HTTPSERVER_HTTPROUTER_H
