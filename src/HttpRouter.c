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

static CachedFile * create_cached_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return NULL;
    CachedFile * content = malloc(sizeof(CachedFile) + st.st_size + 1 + LEN_BUF_SIZE);

    if (!content) {
        perror("Failed to allocate memory.");
        return NULL;
    }

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        free(content);
        perror("Error opening file");
        return NULL;
    }

    content->len = st.st_size;
    const size_t bytes_read = fread(content->data, 1, st.st_size, file);
    content->data[bytes_read] = '\0';
    fclose(file);

    // calculate pointer at the end of the allocated data body.
    char * len_buf = content->data + st.st_size + 1;

    snprintf(len_buf, LEN_BUF_SIZE, "%zu", content->len);

    content->headers[0] = (ResponseHeader){ .key = "Content-Type", .value = get_content_type(path) };
    content->headers[1] = (ResponseHeader){ .key = "Content-Length", .value = len_buf };
    content->headers[2] = (ResponseHeader){ .key = "Cache-Control", .value = "max-age=31536000, immutable" };

    return content;
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

        CachedFile * file = create_cached_file(fpath);
        if (file) {
            cache_file(cache, url, file);
            if (strcmp("index.html", de->d_name) == 0) cache_file(cache, "/", file);
            fcount++;
        }
    }

    closedir(dr);
    return fcount;
}

int cache_file(ContentCache * cache, const char * url_path, CachedFile * file) {
    return dict_insert(cache, url_path, file);
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
    return "";
}

HttpResponse from_cached_file(const HttpRequest * req, const CachedFile * file) {
    const HttpResponse response = {
        .status = 200, .reason = "OK",
        .headers = file->headers,  .header_count = 3,
        .body = file->data,  .body_len = file->len
    };

    return response;
}
