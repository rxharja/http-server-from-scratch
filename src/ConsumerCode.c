//
// Created by RedonXharja on 5/8/2026.
// Temporary file for keeping consumer code separate from http framework code.

#include "../lib/Dictionary.h"

Dictionary * preload_cache(void);

#define MAX_PATH_LEN 1024

typedef struct {
    char * body;
    size_t body_len;
} content;

//TODO: add data structure to cache requested content
int get_content(const char * path, content * res);

Dictionary * preload_cache(void) {
    Dictionary * d = dict_init();

    content * styles = malloc(sizeof(content));
    content * index = malloc(sizeof(content));
    content * fourOhfour = malloc(sizeof(content));
    content * img = malloc(sizeof(content));

    get_content("styles.css", styles);
    get_content("index.html", index);
    get_content("404.html", fourOhfour);
    get_content("img.jpg", img);

    dict_insert(d, "styles.css", styles);
    dict_insert(d, "index.html", index);
    dict_insert(d, "404.html", fourOhfour);
    dict_insert(d, "img.jpg", img);

    return d;
}

void trim_path(const char * path, char * trimmed_path) {
    int ptr = 0;

    while (path[ptr] == '.' || path[ptr] == '/') {
        ptr++;
    }

    strncpy(trimmed_path, path + ptr, MAX_PATH_LEN - 1);
    trimmed_path[MAX_PATH_LEN - 1] = '\0';
}

int get_content(const char * path, content * res) {
    char trimmed_path[MAX_PATH_LEN];

    trim_path(path, trimmed_path);

    FILE *fp = fopen(trimmed_path, "rb");

    if (fp != NULL) {
        if (fseek(fp, 0L, SEEK_END) == 0) {
            const long bufsize = ftell(fp);

            if (bufsize == -1) return 500;

            res->body = malloc(sizeof(char) * (bufsize + 1));
            res->body_len = bufsize;

            if (fseek(fp, 0L, SEEK_SET) != 0) return 500;

            size_t newLen = fread(res->body, sizeof(char), bufsize, fp);

            if ( ferror( fp ) != 0 ) {
                fputs("Error reading file", stderr);
            }
            else {
                res->body[newLen++] = '\0';
            }
        }

        fclose(fp);
    }
    else {
        return 404;
    }

    return 200;
}

