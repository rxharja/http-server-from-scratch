//
// Created by RedonXharja on 5/13/2026.
//

#include "HttpRouter.h"

#include <dirent.h>
#include <sys/stat.h>

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

static CachedFile * create_cached_file(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return NULL;
    CachedFile * content = malloc(sizeof(CachedFile) + st.st_size + 1);

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
