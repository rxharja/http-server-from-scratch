#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "Connection.h"
#include "HttpServer.h"

#define BACKLOG 10

void sigchild_handler(int s) {
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
    errno = saved_errno;
}

static volatile sig_atomic_t keepRunning = 1;

void intHandler(const int sig) {
    (void)sig; // unused
    keepRunning = 0;
}

static HttpResponse not_found(const HttpRequest * request) {
    static const ResponseHeader h[1] = { { "Content-Type", "text/html" }, };
    static const char body[] = "<h1>Nothing here...</h1>\n";
    static const HttpResponse response = {
        .status = 404, .reason = "Not Found",
        .headers = h,  .header_count = 1,
        .body = body,  .body_len = sizeof body - 1
    };
    return response;
}

static HttpResponse do_something(const HttpRequest * request) {
    static const ResponseHeader h[1] = { { "Content-Type", "text/html" }, };
    HttpResponse response = {
        .status = 200, .reason = "OK",
        .headers = h,  .header_count = 1,
        .body_len = request->body_len,
    };
    char *body_buf = malloc(request->body_len);
    // todo: currently the OS reclaims the heap by the process ending
    memcpy(body_buf, request->body, request->body_len);
    response.body = body_buf;
    return response;
}

int main(const int argc, char *argv[]) {
    signal(SIGINT, intHandler);
    struct sigaction sa;

    if (argc != 2) {
       printf("Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (valid_port(argv[1]) != 0) {
        printf("Invalid port number\n");
        exit(EXIT_FAILURE);
    }

    Route routes[2] = {
        { "GET", "/test", not_found },
        { "POST", "/test", do_something }
    };

    const Router router = {
        .static_cache = content_cache_create(),
        .route_count = 2,
        .routes = routes
    };

    cache_static_dir(router.static_cache, "wwwroot", NULL);

    sa.sa_handler = sigchild_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        content_cache_free(router.static_cache);
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    run_server(argv[1], &router, BACKLOG);

    return EXIT_SUCCESS;
}
