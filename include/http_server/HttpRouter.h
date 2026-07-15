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

// The full 2x2 of freshness (static = immutable, dyn = revalidated) x delivery
// (resident = held in memory, streamed = reopened and pumped per request).
// See content_registry_add_file for the per-mode behavior.
typedef enum {
    SERVE_STATIC_RESIDENT,
    SERVE_STATIC_STREAMED,
    SERVE_DYN_RESIDENT,
    SERVE_DYN_STREAMED,
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
 * @return  freshly-allocated content cache; caller frees with content_registry_free()
 */
ContentRegistry *content_registry_create();

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
HttpResponse response_resident(const HttpRequest *req, const ContentEntry *file);

/**
 * Resolve `url_path` in the registry and classify the result:
 *   CONTENT_MISS:         not registered, caller falls through to routes.
 *   CONTENT_GONE:         registered but the backing file stat() failed (404).
 *   CONTENT_NOT_MODIFIED: a dynamic entry whose validators match the request (304).
 *   CONTENT_HIT:          serve the entry. On HIT it holds the current body.
 * Dynamic-resident entries are re-stat'd here and reloaded if the file changed;
 * static and streamed entries resolve straight to HIT.
 *
 * @param registry  content registry
 * @param req       originating request (used for If-None-Match / If-Modified-Since)
 * @param url_path  URL key
 * @return          lookup result; entry is valid for CONTENT_NOT_MODIFIED and CONTENT_HIT
 */
ContentLookupResult content_registry_lookup(ContentRegistry *registry, const HttpRequest *req, const char *url_path);

/**
 * @param f  cache entry the client's validators matched
 * @return   304 Not Modified response (headers only)
 */
HttpResponse response_dynamic_304(const ContentEntry  *f);

/**
 * Sanity check used at startup: flags accidental (method,path) duplicates in the
 * route table.
 *
 * @param router  router to scan
 * @return        non-zero if a duplicate is present, 0 otherwise
 */
int router_has_duplicate_routes(const Router *router);

/**
 *
 * @param req Request used in the case the entry is a dynamic resident
 * @param entry Content entry whose mode is used to route between serving dynamic resident or streamed file.
 * @return HttpResponse based on the entry's mode
 */
HttpResponse response_for_entry(const HttpRequest * req, const ContentEntry * entry);

HttpResponse response_streamed(const ContentEntry * file, Arena * scratch);

/**
 * Register a single file under `url`, choosing its caching and delivery
 * behavior via `mode`:
 *   SERVE_STATIC_RESIDENT: body read into the entry once, never revalidated;
 *                          served with an immutable, cache-forever policy.
 *   SERVE_STATIC_STREAMED: no body stored; the file is reopened and pumped in
 *                          chunks at request time, but served with the same
 *                          immutable cache policy as SERVE_STATIC_RESIDENT. Use
 *                          for large unchanging assets too big to hold resident.
 *   SERVE_DYN_RESIDENT:    body read into the entry, re-stat'd per request,
 *                          reloaded if it changed; carries ETag/Last-Modified.
 *   SERVE_DYN_STREAMED:    no body stored; the file is reopened and pumped in
 *                          chunks at request time, revalidated (no-cache).
 * Both streamed modes omit Content-Length, since streamed bodies are sent with
 * chunked transfer-encoding.
 *
 * @param cache    destination registry
 * @param fs_path  filesystem path of the source file
 * @param url      URL key the file is served under (NUL-terminated)
 * @param mode     caching/delivery behavior (see above)
 * @return         1 on success, 0 on stat or allocation failure
 */
int content_registry_add_file(ContentRegistry * cache, const char * fs_path, const char * url, ServeMode mode);

/**
 * Walk `dir_path` and register every regular file under `url_prefix` with the
 * given `mode`, delegating each file to content_registry_add_file(). Hidden
 * files (leading '.') and non-regular entries are skipped. A file named
 * "index.html" is additionally registered at "/".
 *
 * @param cache       destination registry
 * @param dir_path    filesystem directory to walk
 * @param url_prefix  URL prefix prepended to each entry; may be NULL for "/"
 * @param mode        caching/delivery behavior applied to every file (see
 *                    content_registry_add_file)
 * @return            count of files registered (0 if the directory cannot be opened)
 */
int content_registry_add_dir(ContentRegistry * cache, const char * dir_path, const char * url_prefix, ServeMode mode);

#endif //HTTPSERVER_HTTPROUTER_H
