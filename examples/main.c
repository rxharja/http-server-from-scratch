#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <http_server/HttpServer.h>

#define BACKLOG 10

static HttpResponse entry(const HttpRequest * request) {
    static const ResponseHeader h[1] = { { "Content-Type", "text/html" }, };

    static const char body[] =
        "<h1>First and Last Name</h1>"
        "<form method=POST action=\"/submit\">"
            "<label for=\"fname\">First name:</label>"
            "<input type=\"text\" id=\"fname\" name=\"fname\"><br><br>"

            "<label for=\"lname\">Last name:</label>"
            "<input type=\"text\" id=\"lname\" name=\"lname\"><br><br>"

            "<input type=\"submit\" value=\"Submit\">"
        "</form>";

    static const HttpResponse response = {
        .status = 200, .reason = "OK",
        .headers = h,  .header_count = 1,
        .kind = BODY_BUFFER,
        .body.body_buf = (HttpBuffer) { .buffer = (char*)body, .size = sizeof body - 1, .cap = sizeof body - 1 }
    };
    return response;
}

static HttpResponse submit(const HttpRequest * request) {
    static const ResponseHeader h[1] = { { "Content-Type", "text/html" }, };
    HttpResponse response = {
        .status = 200, .reason = "OK",
        .headers = h,  .header_count = 1,
        .kind = BODY_BUFFER,
        .body.body_buf = (HttpBuffer) { .buffer = NULL, .size = request->body_len, .cap = request->body_len }
    };

    char *body_buf = arena_array(request->scratch, char, request->body_len);
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

    Route routes[2] = {
        { "GET",  "/form", entry },
        { "POST", "/submit", submit }
    };

    const Router router = {
        .registry = content_registry_create(),
        .route_count = 2,
        .routes = routes
    };

    content_registry_add_dir(router.registry, "wwwroot", NULL, SERVE_STATIC_STREAMED);

    server_run(argv[1], &router, BACKLOG);

    content_registry_free(router.registry);

    return EXIT_SUCCESS;
}
