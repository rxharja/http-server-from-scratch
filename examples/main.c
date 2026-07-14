#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <http_server/HttpServer.h>

#define BACKLOG 10

static HttpResponse not_found(const HttpRequest * request) {
    static const ResponseHeader h[1] = { { "Content-Type", "text/html" }, };
    static const char body[] = "<h1 style=\"font-family: sans-serif; color: orange;\">Welcome to my test page</h1>\n";
    static const HttpResponse response = {
        .status = 200, .reason = "OK",
        .headers = h,  .header_count = 1,
        .kind = BODY_BUFFER,
        .body.body_buf = (HttpBuffer) { .buffer = (char*)body, .size = sizeof body - 1, .cap = sizeof body - 1 }
    };
    return response;
}

static HttpResponse do_something(const HttpRequest * request) {
    static const ResponseHeader h[1] = { { "Content-Type", "text/html" }, };
    HttpResponse response = {
        .status = 200, .reason = "OK",
        .headers = h,  .header_count = 1,
        .kind = BODY_BUFFER,
        .body.body_buf = (HttpBuffer) { .buffer = NULL, .size = request->body_len, .cap = request->body_len }
    };

    char *body_buf = arena_alloc(request->scratch, request->body_len, 1);
    if (!body_buf) return response_error_from_status(PARSE_SERVER_ERROR);
    memcpy(body_buf, request->body, request->body_len);
    response.body.body_buf.buffer = body_buf;
    return response;
}

int main(const int argc, char *argv[]) {
    if (argc != 2) {
       printf("Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (server_port_valid(argv[1]) != 0) {
        printf("Invalid port number\n");
        exit(EXIT_FAILURE);
    }

    Route routes[2] = {
        { "GET",  "/test", not_found },
        { "POST", "/test", do_something }
    };

    const Router router = {
        .registry = content_registry_create(),
        .route_count = 2,
        .routes = routes
    };

    content_registry_add_dir(router.registry, "wwwroot", NULL, SERVE_DYN_STREAMED);

    server_run(argv[1], &router, BACKLOG);

    content_registry_free(router.registry);

    return EXIT_SUCCESS;
}
