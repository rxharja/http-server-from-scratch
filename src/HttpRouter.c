//
// Created by RedonXharja on 5/13/2026.
//

#include "HttpRouter.h"

#include <dirent.h>
#include <sys/stat.h>
#include "../lib/parser.h"

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
static int read_file(const char *path, char *dest, const size_t size, size_t *out_len) {
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

static CachedFile *load_static(const char *url_path, const char *fs_path) {
    struct stat st;
    if (stat(fs_path, &st) != 0) return NULL;

    CachedFile *c = malloc(sizeof(*c) + st.st_size + 1 + LEN_BUF_SIZE);
    if (!c) return NULL;

    if (read_file(fs_path, c->data, st.st_size, &c->len) != 0) { free(c); return NULL; }

    char *len_buf = c->data + c->len + 1;
    snprintf(len_buf, LEN_BUF_SIZE, "%zu", c->len);

    c->headers[0] = (ResponseHeader){ "Content-Type",   get_content_type(fs_path) };
    c->headers[1] = (ResponseHeader){ "Content-Length", len_buf };
    c->headers[2] = (ResponseHeader){ "Cache-Control", "max-age=31536000, immutable" };
    return c;
}

static DynamicCachedFile *load_dynamic(const char *url_path, const char *fs_path, const struct stat *st) {
    DynamicCachedFile *d = malloc(sizeof(*d) + st->st_size + 1 + LEN_BUF_SIZE);
    if (!d) return NULL;

    if (read_file(fs_path, d->data, st->st_size, &d->len) != 0) { free(d); return NULL; }
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

    d->headers[0] = (ResponseHeader){ "Content-Type",   get_content_type(fs_path) };
    d->headers[1] = (ResponseHeader){ "Content-Length", len_buf };
    d->headers[2] = (ResponseHeader){ "ETag",           d->etag };
    d->headers[3] = (ResponseHeader){ "Last-Modified",  d->last_modified };
    d->headers[4] = (ResponseHeader){ "Cache-Control",  "no-cache" };
    return d;
}

// won't work on esp32 as there is no fs, todo: set a compilation flag
int cache_static_dir(ContentCache * cache, const char * dir_path, const char * url_prefix) {
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

        CachedFile * file = load_static(url, fpath);
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
    free_dict(cache, free_kvp);
}

char* get_content_type(const char * path) {
    if (ends_with(path, ".html")) return "text/html";
    if (ends_with(path, ".ico")) return "image/x-icon";
    if (ends_with(path, ".jpg") || ends_with(path, ".jpeg")) return "image/jpeg";
    if (ends_with(path, ".png")) return "image/png";
    if (ends_with(path, ".css")) return "text/css";
    if (ends_with(path, ".js")) return "application/javascript";
    if (ends_with(path, ".wasm")) return "application/wasm";
    if (ends_with(path, ".data")) return "application/octet-stream";
    return "application/octet-stream";
}

HttpResponse from_cached_file(const HttpRequest * req, const CachedFile * file) {
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

HttpResponse make_304(const DynamicCachedFile *f) {
    return (HttpResponse) {
        .status = 304, .reason = "Not Modified",
        .headers = &f->headers[2],   // ETag, Last-Modified, Cache-Control
        .header_count = 3,
        .body = NULL, .body_len = 0,
    };
}

HttpResponse from_dynamic_cached_file(const DynamicCachedFile *f) {
    return (HttpResponse) {
        .status = 200, .reason = "OK",
        .headers = f->headers, .header_count = 5,
        .body = f->data, .body_len = f->len,
    };
}

static int client_validators_match(const HttpRequest *req, const DynamicCachedFile *f) {
    const Header *inm = get_header(req->headers, req->header_count, "if-none-match");
    if (inm) {
        if (strcmp(inm->value, "*") == 0) return 1;          // §3.2 wildcard
        return strcmp(inm->value, f->etag) == 0;             // ignore IMS even on miss
    }
    const Header *ims = get_header(req->headers, req->header_count, "if-modified-since");
    if (ims) {
        time_t t;
        if (parse_http_date(ims->value, &t) != 0) return 0;  // malformed → treat as miss
        return f->mtime <= t;
    }
    return 0;
}

DynamicLookupResult dynamic_lookup(ContentCache * cache, const HttpRequest * req, const char * url_path) {
    DynamicLookupResult r = {0};
    DynamicCachedFile *entry = dict_find(cache, url_path);
    if (!entry) { r.status = DYN_NOT_REGISTERED; return r; } // fall through to routes

    struct stat st;
    if (stat(entry->fs_path, &st) != 0) { r.status = DYN_GONE; return r; } // 404

    // dict_insert replaces + frees the old entry
    if (st.st_mtime != entry->mtime || st.st_size != entry->size) {
        DynamicCachedFile *fresh = load_dynamic(url_path, entry->fs_path, &st);
        if (!fresh) { r.status = DYN_GONE; return r; } // 404
        dict_insert(cache, url_path, fresh);
        entry = fresh;
    }

    r.file = entry;
    r.status = client_validators_match(req, entry) ? DYN_NOT_MODIFIED : DYN_HIT;
    return r;
}