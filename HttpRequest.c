//
// Created by redonxharja on 5/27/25.
//

#include <stdio.h>
#include <string.h>
#include "HttpRequest.h"

#include <stdlib.h>

HttpMethod parse_http_method(const char *method) {
    if (strcmp(method, "GET") == 0) return GET;
    if (strcmp(method, "POST") == 0) return POST;
    if (strcmp(method, "PUT") == 0) return PUT;
    if (strcmp(method, "DELETE") == 0) return PUT;
    return UNKNOWN;
}

char* show_http_method(const HttpMethod method) {
    if (method == GET) return "GET";
    if (method == POST) return "POST";
    if (method == PUT) return "PUT";
    if (method == DELETE) return "DELETE";
    return "UNKNOWN";
}

void parse_request(char * message, HttpRequest * req) {
    const char * line = strtok(message, "\r\n");

    if (line != NULL) {
        char method_str[MAX_METHOD_LEN];
        sscanf(line, "%s %s %s", method_str, req->path, req->version);
        req->method = parse_http_method(method_str);
        line = strtok(NULL, "\r\n");
    }

    req->header_count = 0;

    while (line != NULL && strlen(line) > 0) {
        if (req->header_count >= MAX_HEADERS) break;
        const char * colon = strchr(line, ':');

        if (colon != NULL) {
            size_t key_len = colon - line;
            size_t value_len = strlen(colon + 2);

            strncpy(req->headers[req->header_count].key, line, key_len);
            req->headers[req->header_count].key[key_len] = '\0';

            strncpy(req->headers[req->header_count].value, colon + 2, value_len);
            req->headers[req->header_count].value[value_len] = '\0';

            req->header_count++;
        }

        line = strtok(NULL, "\r\n");
    }
}

void show_request(HttpRequest * req) {
    printf("%s %s %s\r\n", show_http_method(req->method), req->path, req->version);

    for (int i = 0; i < req->header_count; i++) {
        printf("%s: %s\r\n", req->headers[i].key, req->headers[i].value);
    }
}

int get_content(const char * path, Content * res) {
    char trimmed_path[MAX_PATH_LEN];

    if (path[0] == '/') {
        strncpy(trimmed_path, path + 1, MAX_PATH_LEN - 1);
        trimmed_path[MAX_PATH_LEN - 1] = '\0';
    }
    else {
        strncpy(trimmed_path, path, MAX_PATH_LEN - 1);
        trimmed_path[MAX_PATH_LEN - 1] = '\0';
    }

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

void set_header(HttpResponse *res, const char *name, const char *value) {
    if (res->headerCount < MAX_HEADERS) {
        strncpy(res->headers[res->headerCount].key, name, MAX_HEADER_KEY_LEN);
        strncpy(res->headers[res->headerCount].value, value, MAX_HEADER_VALUE_LEN);
        res->headerCount++;
    }
}
int EndsWith(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;

    const size_t lenstr = strlen(str);
    const size_t lensuffix = strlen(suffix);

    if (lensuffix >  lenstr) return 0;

    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

char* get_content_type(const char * path) {
    if (EndsWith(path, ".html")) return "text/html";
    if (EndsWith(path, ".ico")) return "image/x-icon";
    if (EndsWith(path, ".jpg") || EndsWith(path, ".jpeg")) return "image/jpeg";
    if (EndsWith(path, ".css")) return "text/css";
    return "";
}

HttpResponse* pack_response(const HttpRequest * req) {
    HttpResponse * res = malloc(sizeof(HttpResponse));
    res->content = malloc(sizeof(Content));

    strcpy(res->version ,"HTTP/1.1");
    res->version[strlen("HTTP/1.1")] = '\0';

    switch (req->method) {
        case GET:
            if (strcmp(req->path, "/") == 0) {
                res->statusCode = get_content("index.html", res->content);
            }
            else {
                res->statusCode = get_content(req->path, res->content);
            }

            switch (res->statusCode) {
                case 200:
                    const char * ok = "OK";
                    strcpy(res->reasonPhrase, ok);
                    res->reasonPhrase[strlen(ok)] = '\0';
                    set_header(res, "Content-Type", get_content_type(req->path));

                    char len[20];
                    sprintf(len, "%lu", res->content->body_len);
                    set_header(res, "Content-Length", len);
                    set_header(res, "Cache-Control", "max-age=86400");
                    break;
                case 404:
                    const char * notFound = "File not Found";
                    strcpy(res->reasonPhrase, notFound);
                    res->reasonPhrase[strlen(notFound)] = '\0';
                    get_content("404.html", res->content);
                    char len404[20];
                    sprintf(len404, "%lu", res->content->body_len);
                    set_header(res, "Content-Length", len404);
                    set_header(res, "Cache-Control", "max-age=86400");
                    break;
                default:
                    const char * error = "Internal Server Error";
                    strcpy(res->reasonPhrase, error);
                    res->reasonPhrase[strlen(error)] = '\0';
                    break;
            }
            break;

        default:
            res->statusCode = 405;
            const char * reason = "Method not allowed";
            strcpy(res->reasonPhrase, reason);
            res->reasonPhrase[strlen(reason)] = '\0';
            break;
    }

    return res;
}

int serialize_response(const HttpResponse * resp, char * buffer, size_t buffer_size) {
    int offset = 0;

    // Start line
    int written = snprintf(buffer + offset, buffer_size - offset,
                           "%s %d %s\r\n", resp->version, resp->statusCode, resp->reasonPhrase);
    if (written < 0 || written >= buffer_size - offset) return -1;
    offset += written;

    // Headers
    for (int i = 0; i < resp->headerCount; i++) {
        written = snprintf(buffer + offset, buffer_size - offset,
                           "%s: %s\r\n", resp->headers[i].key, resp->headers[i].value);
        if (written < 0 || written >= buffer_size - offset) return -1;
        offset += written;
    }

    // Blank line
    if (offset + 2 >= buffer_size) return -1;
    buffer[offset++] = '\r';
    buffer[offset++] = '\n';

    // Body
    if (resp->content->body && resp->content->body_len > 0) {
        if (offset + resp->content->body_len >= buffer_size) return -1;
        memcpy(buffer + offset, resp->content->body, resp->content->body_len);
        offset += resp->content->body_len;
    }

    return offset; // total bytes written
}

void free_response(HttpResponse * res) {
    free(res->content);
    free(res);
}