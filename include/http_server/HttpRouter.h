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

typedef HttpResponse (*handler_fn)(const HttpRequest *req);

typedef HttpResponse (*handler_fn_with_data)(const HttpRequest *req, const void *data);

typedef union {
    handler_fn fn;
    handler_fn_with_data fn_with_data;
} handler;

typedef struct {
    const char *method, *path;
    const handler handler;
    const void *data;
} Route;

typedef enum {
    SERVE_STATIC_RESIDENT,
    SERVE_DYN_RESIDENT,
    SERVE_DYN_STREAMED
} ServeMode;

typedef struct {
    time_t mtime;
    off_t size; // for stat comparison alongside mtime
    char fs_path[256]; // source path, for revalidation re-reads
    char etag[40]; // W/"<mtime>-<size>"
    char last_modified[32]; // RFC 7231 IMF-fixdate
    char len_str[24]; // backs content-length header
} RevalMeta;

typedef struct {
    ServeMode mode;
    size_t len;
    ResponseHeader headers[5]; // [0] content-type [1] content-length [2] ETag [3] last-modified [4] cache-control
    RevalMeta * reval; // NULL for static
    char data[]; // file bytes, 0 length for streamed
} ContentEntry;

typedef enum {
    CONTENT_MISS, // url not in cache -> fall through to routes
    CONTENT_GONE, // url registered but stat() failed -> 404
    CONTENT_NOT_MODIFIED, // client's validators match -> 304
    CONTENT_HIT // serve full body and invalidate cache
} ContentLookupStatus;

typedef struct {
    ContentLookupStatus status; // static entries can ONLY produce HIT or MISS
    const ContentEntry *entry; // valid for NOT MODIFIED or HIT
} ContentLookupResult;

typedef struct {
    const Route *route; // NULL if no exact match
    const char *allowed[8]; // methods registered for this path
    size_t allowed_count; // 0 → 404. >0 with route==NULL → 405.
} RouteLookupResult;

typedef Dictionary ContentRegistry;

typedef struct {
    const Route *routes;
    const size_t route_count;
    ContentRegistry *registry; // immutable, no revalidation
} Router;

/**
 * Look up `path` across `routes`. On exact (method,path) hit, `route` is set.
 * On path-only hit (wrong method), `route` is NULL and `allowed[]` lists the
 * methods registered for that path so the caller can emit a 405 + Allow.
 *
 * @param routes  route table
 * @param count   number of entries in `routes`
 * @param method  request method as a NUL-terminated string ("GET", "POST", ...)
 * @param path    request path (NUL-terminated, query stripped)
 * @return        lookup result; see RouteLookupResult docs
 */
RouteLookupResult route_lookup(const Route routes[], size_t count, const char *method, const char *path);

/**
 * @return  freshly-allocated content cache; caller frees with content_cache_free()
 */
ContentRegistry *content_registry_create();

/**
 * Walk `dir_path` and cache every regular file under `url_prefix`. Cached files
 * are served statically without re-stat.
 *
 * @param cache       destination cache
 * @param dir_path    filesystem directory to walk
 * @param url_prefix  URL prefix prepended to each cached path; may be NULL for "/"
 * @return            0 on success, non-zero on I/O error
 */
int static_dir_cache(ContentRegistry *cache, const char *dir_path, const char *url_prefix);

/**
 * @param cache     destination cache
 * @param url_path  URL key (NUL-terminated)
 * @param file      cached entry; ownership transfers to the cache
 * @return          0 on success, non-zero on allocation failure
 */
int cache_file(ContentRegistry *cache, const char *url_path, ContentEntry *file);

/**
 * @param cache  cache to free; safe to pass NULL
 */
void content_registry_free(ContentRegistry *cache);

/**
 * @param path  filesystem or URL path
 * @return      static MIME type string inferred from extension; defaults to "application/octet-stream"
 */
char *content_type(const char *path);

/**
 * Build a 200 response from a pre-cached static file.
 *
 * @param req   originating request (used for HEAD detection / validators)
 * @param file  cached file entry
 */
HttpResponse response_cached(const HttpRequest *req, const ContentEntry *file);

/**
 * Re-stats the backing file and returns DYN_NOT_REGISTERED / DYN_GONE /
 * DYN_NOT_MODIFIED / DYN_HIT. On DYN_HIT the cache entry holds the current body.
 *
 * @param registry     dynamic cache
 * @param req       originating request (used for If-None-Match / If-Modified-Since)
 * @param url_path  URL key
 */
ContentLookupResult content_registry_lookup(ContentRegistry *registry, const HttpRequest *req, const char *url_path);

/**
 * @param f  cache entry the client's validators matched
 * @return   304 Not Modified response (headers only)
 */
HttpResponse response_dynamic_304(const ContentEntry  *f);

/**
 * @param f  cache entry to serve
 * @return   200 response with body + ETag + Last-Modified
 */
HttpResponse response_dynamic(const ContentEntry  *f);

/**
 * Sanity check used at startup: flags accidental (method,path) duplicates in the
 * route table.
 *
 * @param router  router to scan
 * @return        non-zero if a duplicate is present, 0 otherwise
 */
int router_has_duplicate_routes(const Router *router);

#endif //HTTPSERVER_HTTPROUTER_H
