//
// Created by RedonXharja on 5/13/2026.
//

#include "http_server/HttpRouter.h"

#include <dirent.h>
#include <sys/stat.h>
#include "parser.h"

RouteLookupResult route_lookup(const Route routes[], const size_t count, const char *method, const char *path) {
    RouteLookupResult res = {0};
    for (int i = 0; i < count; i++) {
        const int path_match = strcmp(path, routes[i].path) == 0;
        if (!path_match) continue;
        if (strcmp(method, routes[i].method) == 0) {
            res.route = &routes[i];
            return res;
        }
        if (res.allowed_count < 8) {
            res.allowed[res.allowed_count++] = routes[i].method;
        }
    }
    return res;
}

ContentCache * content_cache_create() {
    return dict_init();
}

#define LEN_BUF_SIZE 32

// Reads up to `size` bytes from `path` into `dest`, NUL-terminates at the actual
// read length, and writes the count into *out_len. Returns 0 on success.
static int file_read(const char *path, char *dest, const size_t size, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror("fopen"); return -1; }
    const size_t got = fread(dest, 1, size, f);
    const int err = ferror(f);
    fclose(f);
    if (err) return -1;
    dest[got] = '\0';
    *out_len = got;
    return 0;
}

static CachedFile *cached_file_static_load(const char *url_path, const char *fs_path) {
    struct stat st;
    if (stat(fs_path, &st) != 0) return NULL;

    CachedFile *c = malloc(sizeof(*c) + st.st_size + 1 + LEN_BUF_SIZE);
    if (!c) return NULL;

    if (file_read(fs_path, c->data, st.st_size, &c->len) != 0) { free(c); return NULL; }

    char *len_buf = c->data + c->len + 1;
    snprintf(len_buf, LEN_BUF_SIZE, "%zu", c->len);

    c->headers[0] = (ResponseHeader){ "Content-Type",   content_type_get(fs_path) };
    c->headers[1] = (ResponseHeader){ "Content-Length", len_buf };
    c->headers[2] = (ResponseHeader){ "Cache-Control", "max-age=31536000, immutable" };
    return c;
}

static DynamicCachedFile *cached_file_dynamic_load(const char *url_path, const char *fs_path, const struct stat *st) {
    DynamicCachedFile *d = malloc(sizeof(*d) + st->st_size + 1 + LEN_BUF_SIZE);
    if (!d) return NULL;

    if (file_read(fs_path, d->data, st->st_size, &d->len) != 0) { free(d); return NULL; }
    d->mtime = st->st_mtime;
    d->size  = st->st_size;

    strncpy(d->fs_path, fs_path, sizeof d->fs_path - 1);
    d->fs_path[sizeof d->fs_path - 1] = '\0';

    snprintf(d->etag, sizeof d->etag, "W/\"%lx-%lx\"",
             (unsigned long)st->st_mtime, (unsigned long)st->st_size);

    const struct tm *gmt = gmtime(&st->st_mtime);
    strftime(d->last_modified, sizeof d->last_modified, "%a, %d %b %Y %H:%M:%S GMT", gmt);

    char *len_buf = d->data + d->len + 1;
    snprintf(len_buf, LEN_BUF_SIZE, "%zu", d->len);

    d->headers[0] = (ResponseHeader){ "Content-Type",   content_type_get(fs_path) };
    d->headers[1] = (ResponseHeader){ "Content-Length", len_buf };
    d->headers[2] = (ResponseHeader){ "ETag",           d->etag };
    d->headers[3] = (ResponseHeader){ "Last-Modified",  d->last_modified };
    d->headers[4] = (ResponseHeader){ "Cache-Control",  "no-cache" };
    return d;
}

// won't work on esp32 as there is no fs, todo: set a compilation flag
int static_dir_cache(ContentCache * cache, const char * dir_path, const char * url_prefix) {
    struct dirent *de;  // Pointer for directory entry
    DIR *dr = opendir(dir_path); // Open current directory
    if (dr == NULL) {
        printf("Could not open current directory");
        return 0;
    }

    int fcount = 0;
    while ((de = readdir(dr)) != NULL) {
        if (de->d_name[0] == '.') continue; // Skip hidden files and navigation directories
        if (de->d_type != DT_REG) continue; // Only process regular files

        char url[512];
        char fpath[512];

        if (url_prefix) snprintf(url, sizeof(url), "%s/%s", url_prefix, de->d_name);
        else snprintf(url, sizeof(url), "/%s", de->d_name);

        snprintf(fpath, sizeof(fpath), "%s/%s", dir_path, de->d_name);

        CachedFile * file = cached_file_static_load(url, fpath);
        if (file) {
            dict_insert(cache, url, file);
            if (strcmp("index.html", de->d_name) == 0) dict_insert(cache, "/", file);
            fcount++;
        }
    }

    closedir(dr);
    return fcount;
}

void content_cache_free(ContentCache * cache) {
    // pass 'free' because CachedFile is a single allocation
    // flexible array member 'data' is freed along with the struct
    dict_free(cache, kvp_free);
}

char* content_type_get(const char * path) {
    if (str_ends_with(path, ".html")) return "text/html";
    if (str_ends_with(path, ".ico")) return "image/x-icon";
    if (str_ends_with(path, ".jpg") || str_ends_with(path, ".jpeg")) return "image/jpeg";
    if (str_ends_with(path, ".png")) return "image/png";
    if (str_ends_with(path, ".css")) return "text/css";
    if (str_ends_with(path, ".js")) return "application/javascript";
    if (str_ends_with(path, ".wasm")) return "application/wasm";
    if (str_ends_with(path, ".data")) return "application/octet-stream";
    return "application/octet-stream";
}

HttpResponse response_cached(const HttpRequest * req, const CachedFile * file) {
    HttpResponse res = {
        .status = 200, .reason = "OK",
        .head_only = req->request_line.method == HEAD,
        .headers = file->headers,  .header_count = 3,
    };

    if (!res.head_only) {
        res.body = file->data;
        res.body_len = file->len;
    }

    return res;
}

HttpResponse response_dynamic_304(const DynamicCachedFile *f) {
    return (HttpResponse) {
        .status = 304, .reason = "Not Modified",
        .headers = &f->headers[2],   // ETag, Last-Modified, Cache-Control
        .header_count = 3,
        .body = NULL, .body_len = 0,
    };
}

HttpResponse response_dynamic(const DynamicCachedFile *f) {
    return (HttpResponse) {
        .status = 200, .reason = "OK",
        .headers = f->headers, .header_count = 5,
        .body = f->data, .body_len = f->len,
    };
}

static int request_validators_match(const HttpRequest *req, const DynamicCachedFile *f) {
    const Header *inm = header_find(req->headers, req->header_count, "if-none-match");
    if (inm) {
        if (strcmp(inm->value, "*") == 0) return 1;          // §3.2 wildcard
        return strcmp(inm->value, f->etag) == 0;             // ignore IMS even on miss
    }
    const Header *ims = header_find(req->headers, req->header_count, "if-modified-since");
    if (ims) {
        time_t t;
        if (http_date_parse(ims->value, &t) != 0) return 0;  // malformed → treat as miss
        return f->mtime <= t;
    }
    return 0;
}

DynamicLookupResult cache_dynamic_lookup(ContentCache * cache, const HttpRequest * req, const char * url_path) {
    DynamicLookupResult r = {0};
    DynamicCachedFile *entry = dict_find(cache, url_path);
    if (!entry) { r.status = DYN_NOT_REGISTERED; return r; } // fall through to routes

    struct stat st;
    if (stat(entry->fs_path, &st) != 0) { r.status = DYN_GONE; return r; } // 404

    // if modified times don't match or the sizes have changed
    if (st.st_mtime != entry->mtime || st.st_size != entry->size) {
        DynamicCachedFile *fresh = cached_file_dynamic_load(url_path, entry->fs_path, &st);
        if (!fresh) { r.status = DYN_GONE; return r; } // 404
        dict_insert(cache, url_path, fresh); // dict_insert replaces + frees the old entry
        entry = fresh;
    }

    r.file = entry;
    r.status = request_validators_match(req, entry) ? DYN_NOT_MODIFIED : DYN_HIT;
    return r;
}

int router_has_duplicate_routes(const Router * router) {
    const char * get = http_method_show(GET);

    for (int i = 0; i < router->route_count; i++) {
        const Route * route = &router->routes[i];
        if (route->method != get) continue;
        const CachedFile * file = dict_find(router->static_cache, route->path);
        if (file) return 1;
    }

    for (int i = 0; i < router->route_count - 1; i++) {
        const Route * route1 = &router->routes[i];
        for (int j = 1; j < router->route_count; j++) {
            const Route * route2 = &router->routes[j];
            if (strcmp(route1->path, route2->path) == 0 && strcmp(route1->method, route2->method) == 0) return 1;
        }
    }

    return 0;
}

