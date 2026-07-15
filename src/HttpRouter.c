//
// Created by RedonXharja on 5/13/2026.
//

#include "http_server/HttpRouter.h"

#include <stdlib.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <asm-generic/errno-base.h>
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

ContentRegistry * content_registry_create() {
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

static void reval_fill(RevalMeta * reval, const struct stat * st, const char * fs_path) {
    assert(reval);
    assert(st);
    assert(fs_path);

    reval->mtime = st->st_mtime;
    reval->size  = st->st_size;

    strncpy(reval->fs_path, fs_path, sizeof reval->fs_path - 1);
    reval->fs_path[sizeof reval->fs_path - 1] = '\0';

    snprintf(reval->etag, sizeof reval->etag, "W/\"%lx-%lx\"",
             (unsigned long)st->st_mtime, (unsigned long)st->st_size);

    const struct tm *gmt = gmtime(&st->st_mtime);
    strftime(reval->last_modified, sizeof reval->last_modified, "%a, %d %b %Y %H:%M:%S GMT", gmt);
}

static ContentEntry *content_entry_static_load(const char *url_path, const char *fs_path) {
    struct stat st;
    if (stat(fs_path, &st) != 0) return NULL;

    ContentEntry *c = malloc(sizeof(*c) + st.st_size + 1 + LEN_BUF_SIZE);
    if (!c) return NULL;

    if (file_read(fs_path, c->data, st.st_size, &c->len) != 0) { free(c); return NULL; }

    char *len_buf = c->data + c->len + 1;
    snprintf(len_buf, LEN_BUF_SIZE, "%zu", c->len);

    c->headers[0] = (ResponseHeader){ "Content-Type",   content_type(fs_path) };
    c->headers[1] = (ResponseHeader){ "Content-Length", len_buf };
    c->headers[2] = (ResponseHeader){ "Cache-Control", "max-age=31536000, immutable" };
    c->mode = SERVE_STATIC_RESIDENT;
    c->reval = NULL; // not used for static entries
    return c;
}

static ContentEntry *content_entry_dynamic_load(const char *url_path, const char *fs_path, const struct stat *st) {
    ContentEntry *d = malloc(sizeof(*d) + st->st_size + 1);
    if (!d) return NULL;

    d->reval = malloc(sizeof(RevalMeta));

    if (!d->reval) {
        free(d);
        return NULL;
    }

    if (file_read(fs_path, d->data, st->st_size, &d->len) != 0) { free(d->reval); free(d); return NULL; }

    reval_fill(d->reval, st, fs_path);

    char *len_buf = d->data + d->len + 1;
    snprintf(len_buf, LEN_BUF_SIZE, "%zu", d->len);

    d->headers[0] = (ResponseHeader){ "Content-Type",   content_type(fs_path) };
    d->headers[1] = (ResponseHeader){ "Content-Length", len_buf };
    d->headers[2] = (ResponseHeader){ "ETag",           d->reval->etag };
    d->headers[3] = (ResponseHeader){ "Last-Modified",  d->reval->last_modified };
    d->headers[4] = (ResponseHeader){ "Cache-Control",  "no-cache" };
    d->mode = SERVE_DYN_RESIDENT; // todo: handle stream
    return d;
}

void content_registry_free(ContentRegistry * cache) {
    // pass 'free' because CachedFile is a single allocation
    // flexible array member 'data' is freed along with the struct
    dict_free(cache, kvp_free);
}

char* content_type(const char * path) {
    assert(path);
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

// the following functions must be here due to the dependency on CachedFile and DynamicCachedFile
HttpResponse response_resident(const HttpRequest * req, const ContentEntry * file) {
    assert(file);
    assert(req);

    // dynamic content contains ETag and Last Modified headers as well
    const size_t header_count = file->mode == SERVE_STATIC_RESIDENT ? 3 : 5;

    HttpResponse res = {
        .status = 200, .reason = "OK",
        .head_only = req->request_line.method == HEAD,
        .headers = file->headers,  .header_count = header_count,
        .kind = BODY_BUFFER
    };

    // TODO: address if it's streaming or not
    if (!res.head_only) {
        res.body.body_buf.buffer = (char*)file->data;
        res.body.body_buf.size = file->len;
    }

    return res;
}

// a built-in framework cast of void * ctx below
typedef struct { int fd; } FileCtx;

static ssize_t file_pull(void * ctx, char * out, const size_t cap) {
    const int fd = ((FileCtx*)ctx)->fd;
    ssize_t n;
    do { n = read(fd, out, cap); } while (n < 0 && errno == EINTR);
    return n; // > 0 bytes, 0 EOF, < 0 error
}

static void file_close(void * ctx) {
    close(((FileCtx*)ctx)->fd);
}

static Stream file_stream_open(const char * fs_path, Arena * scratch) {
    const int fd = open(fs_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return (Stream){0};

    FileCtx * c = arena_new(scratch, FileCtx);
    if (!c) { close(fd); return (Stream){0}; }
    c->fd = fd;
    return (Stream) { .ctx = c, .pull = file_pull, .cleanup = file_close, };
}

HttpResponse response_streamed(const ContentEntry * file, Arena * scratch) {
    assert(file);
    assert(scratch);
    assert(file->reval);
    assert(file->reval->fs_path);

    const Stream s = file_stream_open(file->reval->fs_path, scratch);
    if (!s.pull) return response_error_from_status(PARSE_SERVER_ERROR);

    // 4 is the number of headers we set when in streaming mode, see content_registry_add_dir below.
    return response_stream(200, "OK", s.pull, s.ctx, s.cleanup, file->headers, 4);
}

HttpResponse response_dynamic_304(const ContentEntry  *f) {
    assert(f);
    // headers[2] -> ETag, Last-Modified, Cache-Control
    return response_none(304, "Not Modified", &f->headers[2], 3);
}

static int request_validators_match(const HttpRequest *req, const ContentEntry  *f) {
    assert(req);
    assert(f);
    assert(f->reval);

    const Header *inm = header_find(req->headers, req->header_count, "if-none-match");
    if (inm) {
        if (strcmp(inm->value, "*") == 0) return 1;          // §3.2 wildcard
        return strcmp(inm->value, f->reval->etag) == 0;             // ignore IMS even on miss
    }
    const Header *ims = header_find(req->headers, req->header_count, "if-modified-since");
    if (ims) {
        time_t t;
        if (http_date_parse(ims->value, &t) != 0) return 0;  // malformed → treat as miss
        return f->reval->mtime <= t;
    }
    return 0;
}

ContentLookupResult content_registry_lookup(ContentRegistry * registry, const HttpRequest * req, const char * url_path) {
    ContentEntry *entry = dict_find(registry, url_path);
    if (!entry) return (ContentLookupResult) { .status = CONTENT_MISS }; // fall through to routes

    switch (entry->mode) {
        case SERVE_DYN_RESIDENT: {
            struct stat st;

            if (stat(entry->reval->fs_path, &st) != 0) return (ContentLookupResult) { .status = CONTENT_GONE }; // 404

            // if modified times don't match or the sizes have changed
            if (st.st_mtime != entry->reval->mtime || st.st_size != entry->reval->size) {
                ContentEntry *fresh = content_entry_dynamic_load(url_path, entry->reval->fs_path, &st);
                if (!fresh) return (ContentLookupResult) { .status = CONTENT_GONE }; // 404
                dict_insert(registry, url_path, fresh); // dict_insert replaces + frees the old entry
                entry = fresh;
            }

            return (ContentLookupResult) {
                .status = request_validators_match(req, entry) ? CONTENT_NOT_MODIFIED : CONTENT_HIT,
                .entry = entry
            };
        }

        case SERVE_DYN_STREAMED: {
            return (ContentLookupResult) { .status = CONTENT_HIT, .entry = entry };
        }

        case SERVE_STATIC_STREAMED:
        case SERVE_STATIC_RESIDENT:
        default: return (ContentLookupResult) { .status = CONTENT_HIT, .entry = entry };

    }

}

int router_has_duplicate_routes(const Router * router) {
    const char * get = http_method_show(GET);

    for (int i = 0; i < router->route_count; i++) {
        const Route * route = &router->routes[i];
        if (route->method != get) continue;
        const ContentEntry * file = dict_find(router->registry, route->path);
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

HttpResponse response_for_entry(const HttpRequest * req, const ContentEntry * entry) {
     switch (entry->mode) {
        case SERVE_STATIC_RESIDENT:
        case SERVE_DYN_RESIDENT: return response_resident(req, entry);
        case SERVE_STATIC_STREAMED:
        case SERVE_DYN_STREAMED: return response_streamed(entry, req->scratch);
     }

    return response_error_from_status(PARSE_SERVER_ERROR);
}

static ContentEntry * content_entry_streamed_load(const char * fs_path, const struct stat * st, const ServeMode mode, const char * cache_control) {
    ContentEntry * entry = malloc(sizeof *entry);
    if (!entry) return NULL;
    entry->mode = mode;
    entry->len = 0;

    entry->reval = malloc(sizeof(RevalMeta));
    if (!entry->reval) { free(entry); return NULL; }
    reval_fill(entry->reval, st, fs_path);

    entry->headers[0] = (ResponseHeader){ .key = "Content-Type",  .value = content_type(fs_path) };
    entry->headers[1] = (ResponseHeader){ .key = "ETag",          .value = entry->reval->etag };
    entry->headers[2] = (ResponseHeader){ .key = "Last-Modified", .value = entry->reval->last_modified };
    entry->headers[3] = (ResponseHeader){ .key = "Cache-Control", .value = cache_control };

    return entry;
}


int content_registry_add_file(ContentRegistry * cache, const char * fs_path, const char * url, const ServeMode mode) {
    struct stat st;
    if (stat(fs_path, &st) != 0) return 0;

    switch (mode) {
        case SERVE_STATIC_RESIDENT: {
            ContentEntry * entry = content_entry_static_load(url, fs_path);
            if (entry == NULL) return 0;
            dict_insert(cache, url, entry);
            return 1;
        }
        case SERVE_STATIC_STREAMED: {
           ContentEntry * entry = content_entry_streamed_load(fs_path, &st, SERVE_STATIC_STREAMED,
               "max-age=31536000, immutable");
            if (!entry) return 0;
            dict_insert(cache, url, entry);
            return 1;
        }
        case SERVE_DYN_RESIDENT: {
            ContentEntry * entry = content_entry_dynamic_load(url, fs_path, &st);
            if (entry == NULL) return 0;
            dict_insert(cache, url, entry);
            return 1;
        }
        case SERVE_DYN_STREAMED: {
           ContentEntry * entry = content_entry_streamed_load(fs_path, &st, SERVE_DYN_STREAMED, "no-cache");
            if (!entry) return 0;
            dict_insert(cache, url, entry);
            return 1;
        }
    }

    return 0;
}

int content_registry_add_dir(ContentRegistry * cache, const char * dir_path, const char * url_prefix, const ServeMode mode) {
    struct dirent *de;  // Pointer for directory entry
    DIR *dr = opendir(dir_path); // Open current directory
    if (dr == NULL) {
        printf("Could not open current directory");
        return 0;
    }

    int f_count = 0;
    while ((de = readdir(dr)) != NULL) {
        if (de->d_name[0] == '.') continue; // Skip hidden files and navigation directories
        if (de->d_type != DT_REG) continue; // Only process regular files

        char url[512];
        char fpath[512];

        if (url_prefix) snprintf(url, sizeof(url), "%s/%s", url_prefix, de->d_name);
        else snprintf(url, sizeof(url), "/%s", de->d_name);

        snprintf(fpath, sizeof(fpath), "%s/%s", dir_path, de->d_name);

        f_count += content_registry_add_file(cache, fpath, url, mode);

        if (strcmp("index.html", de->d_name) == 0) content_registry_add_file(cache, fpath, "/", mode);
    }

    closedir(dr);
    return f_count;
}
