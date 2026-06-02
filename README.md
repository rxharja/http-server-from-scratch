# httpserver

A small, dependency-free HTTP/1.1 server written from scratch in C, targeting
Linux/POSIX. Distributed as a static library: consumers register routes and an
optional content cache, then call `run_server()`.

The eventual goal is a port to ESP32-class hardware; see [Roadmap](#roadmap)
for what that requires.

## Status

Implements the request-handling subset of [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112)
sufficient for serving static files and dynamic responses, with keep-alive,
chunked request decoding, and conditional GETs against cached files. 402 parser
tests covering request lines, headers, body decoding, and HTTP dates.

## Features

- Single-threaded non-blocking I/O via `poll()`; per-connection state machine drives the request lifecycle across wake-ups
- HTTP/1.1 keep-alive, with `Connection` negotiation
- Static file caching with `Content-Length`, `Content-Type`, `Cache-Control`
- Dynamic file serving with `ETag` + `Last-Modified` revalidation (304 responses)
- Chunked `Transfer-Encoding` decoding on requests
- `Expect: 100-continue` handled: server emits an interim `100 Continue` before reading the body when the client asks for it and a body is actually coming; skipped when there's no body, when bytes are already buffered, or for HTTP/1.0
- `HEAD` handled distinctly from `GET`
- 405 responses with `Allow` headers when a path is registered under a different method
- Request smuggling defenses: rejects requests carrying both `Content-Length` and `Transfer-Encoding`, and `HEAD` requests with a body
- No runtime dependencies; POSIX-only (`_XOPEN_SOURCE=700`)

## Quick example

```c
#include <http_server/HttpServer.h>

static HttpResponse hello(const HttpRequest *req) {
    static const ResponseHeader h[] = {{ "Content-Type", "text/plain" }};
    static const char body[] = "hello\n";
    return (HttpResponse){
        .status = 200, .reason = "OK",
        .headers = h, .header_count = 1,
        .body = body, .body_len = sizeof body - 1,
    };
}

int main(int argc, char **argv) {
    if (argc != 2 || valid_port(argv[1]) != 0) return 1;

    Route routes[] = {
        { "GET", "/hello", hello },
    };

    Router router = {
        .routes = routes,
        .route_count = 1,
        .static_cache = content_cache_create(),
    };
    cache_static_dir(router.static_cache, "wwwroot", NULL);

    run_server(argv[1], &router, /*backlog=*/ 10);
    content_cache_free(router.static_cache);
    return 0;
}
```

## Build

Requires CMake 3.30+ and a C11 compiler.

```sh
cmake -S . -B build
cmake --build build
```

Three targets are produced:

| Target            | What it is                                  |
| ----------------- | ------------------------------------------- |
| `HttpServer`      | Static library (`libHttpServer.a`)          |
| `example_server`  | Runnable demo serving the bundled `wwwroot` |
| `HttpServerTests` | Parser test suite                           |

Run the example:

```sh
./build/example_server 8080
```

Run the tests:

```sh
./build/HttpServerTests
```

## Using as a dependency

Add the repo as a subdirectory of your own CMake project:

```cmake
add_subdirectory(third_party/httpserver)

add_executable(myapp src/main.c)
target_link_libraries(myapp PRIVATE HttpServer)
```

`HttpServer`'s `PUBLIC` include directories propagate automatically, so `myapp`
can `#include <http_server/HttpServer.h>` without any further configuration.

## Project layout

```
include/http_server/   public API surface
src/                   implementation + private headers
examples/main.c        runnable demo app
tests/                 parser test suite
```

Implementation details: socket plumbing (`Connection.h`), the byte-level
parser (`parser.h`), and supporting types, live in `src/` and are unreachable
through the consumer's include path. The library's ABI is everything under
`include/http_server/` and nothing else.

## Known compliance gaps

- Chunked `Transfer-Encoding` supported on requests, not yet on responses
- Absolute-form request targets (`GET http://host/path HTTP/1.1`) not parsed; affects proxy use
- Chunked trailer fields not supported (RFC 9112 §7.1.2)

## Roadmap

The current implementation targets POSIX systems with generous memory budgets
(the response buffer is 128 MB per connection, headers up to 8 MB). Embedded
deployment is planned, and would require:

- **Buffer sizing.** Make every `*_MAX_*` and buffer-size macro tunable at compile
  time, and move `HttpRequest::body` out of the struct so a parsed request isn't
  a megabyte.
- **Streaming responses.** Today `HttpResponse` holds a fully-materialized
  `body` + `body_len`; an embedded build needs a callback/pull-based body API so
  large or generated responses don't have to fit in RAM. Streaming response
  bodies also unblock chunked `Transfer-Encoding` on the outbound side.
- **Allocation strategy.** Replace per-request `malloc()` with either fully
  static buffers or a per-connection arena allocator, to avoid heap fragmentation
  on long-running devices. The arena variant also gives consumer handlers a
  scoped scratch space for per-request allocations.
- **Platform abstraction.** A small `platform.h` over the socket and file I/O
  primitives, with POSIX and lwIP implementations behind it. Remove
  Linux-specific headers (e.g. `asm-generic/errno-base.h`).

Already done:

- **Concurrency model.** Replaced the original fork-per-connection model with a
  single-threaded `poll()`-based event loop. Each connection carries an explicit
  phase enum (`CONN_READING_REQUEST → CONN_READING_BODY_* → CONN_BUILDING →
  CONN_SENDING_RESPONSE`) and a tagged-union arm holding the per-phase scratch;
  the dispatcher runs each connection forward until it would block, then yields
  back to the poll loop. No `fork()`, no threads. Portable to RTOS-class
  hardware.
