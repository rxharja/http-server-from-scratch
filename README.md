# httpserver

A small, dependency-free HTTP/1.1 server written from scratch in C, targeting
Linux/POSIX. Distributed as a static library: consumers register routes and an
optional content cache, then call `run_server()`.

The eventual goal is a port to ESP32-class hardware; see [Roadmap](#roadmap)
for what that requires.

## Status

Implements the request-handling subset of [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112)
sufficient for serving static files and dynamic responses (buffered or streamed),
with keep-alive, chunked request decoding, conditional GETs against cached files,
and chunked `Transfer-Encoding` on responses. 520 tests covering request parsing
(request lines, headers, body decoding, HTTP dates), response building, and file
streaming.

## Features

- Single-threaded non-blocking I/O via `poll()`; per-connection state machine drives the request lifecycle across wake-ups
- HTTP/1.1 keep-alive, with `Connection` negotiation
- Static file caching with `Content-Length`, `Content-Type`, `Cache-Control`
- Dynamic file serving with `ETag` + `Last-Modified` revalidation (304 responses)
- Streaming responses via chunked `Transfer-Encoding`: bodies are pulled in fixed-size chunks and never fully materialized, so a file larger than available RAM can still be served
- Content registry with per-file delivery modes (static-resident, dynamic-resident, dynamic-streamed); register a single file or a whole directory
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
        .kind = BODY_BUFFER,
        .body.body_buf = { .buffer = (char*)body, .size = sizeof body - 1, .cap = sizeof body - 1 },
    };
}

int main(int argc, char **argv) {
    if (argc != 2 || server_port_valid(argv[1]) != 0) return 1;

    Route routes[] = {
        { "GET", "/hello", hello },
    };

    Router router = {
        .routes = routes,
        .route_count = 1,
        .registry = content_registry_create(),
    };
    // Serve a directory; SERVE_DYN_STREAMED streams each file in chunks rather
    // than buffering it, so files larger than RAM are fine.
    content_registry_add_dir(router.registry, "wwwroot", NULL, SERVE_DYN_STREAMED);

    server_run(argv[1], &router, /*backlog=*/ 10);
    content_registry_free(router.registry);
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

## Configuration

Every buffer size and protocol limit is a compile-time knob, collected in one
place: [`include/http_server/Config.h`](include/http_server/Config.h). Each is
`#ifndef`-guarded, so you override any of them without editing the file — either
on the compiler command line or by defining it before including any
`http_server` header:

```sh
cc -DHTTP_MAX_HEADERS=16 -DHTTP_MAX_BODY_LEN=8192 ...
```

```cmake
add_compile_definitions(HTTP_MAX_HEADERS=16 HTTP_MAX_BODY_LEN=8192)
```

The defaults target POSIX systems with generous memory; constrained targets
(e.g. ESP32-class hardware) will want to shrink most of them.

| Macro                       | Default | Governs                                            |
| --------------------------- | ------: | -------------------------------------------------- |
| `HTTP_MAX_PATH_LEN`         |    2048 | Request path bytes                                 |
| `HTTP_MAX_QUERY_LEN`        |     512 | Query-string bytes                                 |
| `HTTP_MAX_REQUEST_LEN`      |    8 MiB | Total received request bytes (line + headers + body) |
| `HTTP_MAX_HEADERS`          |      32 | Header lines per request                           |
| `HTTP_MAX_HEADER_KEY_LEN`   |      64 | Header name bytes                                  |
| `HTTP_MAX_HEADER_VALUE_LEN` |     256 | Header value bytes                                 |
| `HTTP_MAX_BODY_LEN`         |   ~1 MiB | Request body / Content-Length cap                  |
| `HTTP_MAX_DECHUNK_SIZE`     |   16 KiB | Decoded chunked-body payload cap                   |
| `HTTP_RESPONSE_BUFFER_SIZE` |    8 KiB | Per-connection response / stream staging buffer    |
| `HTTP_STREAM_CHUNK_SIZE`    |    4 KiB | Bytes pulled from a stream producer per iteration  |
| `HTTP_MAX_REQUESTS`         |     100 | Requests served per keep-alive connection          |
| `HTTP_BUCKET`               |    1024 | Content-registry hash-table bucket count           |

(`HTTP_VERSION_LEN` and `HTTP_CHUNK_LINE_SIZE` are also in `Config.h` but are
protocol-fixed, not tuning knobs.)

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

- Absolute-form request targets (`GET http://host/path HTTP/1.1`) not parsed; affects proxy use
- Chunked trailer fields not supported (RFC 9112 §7.1.2)

## Roadmap

The current implementation targets POSIX systems with generous memory budgets
(the response buffer is 128 MB per connection, headers up to 8 MB). Embedded
deployment is planned, and would require:

- **Buffer sizing.** Every `*_MAX_*` and buffer-size macro is now tunable at
  compile time via [`Config.h`](include/http_server/Config.h) (see
  [Configuration](#configuration)). Still to do: move `HttpRequest::body` out of
  the struct so a parsed request isn't a megabyte.
- **Allocation strategy.** Replace per-request `malloc()` with either fully
  static buffers or a per-connection arena allocator, to avoid heap fragmentation
  on long-running devices. The arena variant also gives consumer handlers a
  scoped scratch space for per-request allocations.
- **Platform abstraction.** A small `platform.h` over the socket and file I/O
  primitives, with POSIX and lwIP implementations behind it. Remove
  Linux-specific headers (e.g. `asm-generic/errno-base.h`).

Already done:

- **Streaming responses.** `HttpResponse` is now a tagged union whose body can be
  a materialized buffer or a pull-based `Stream` (`ctx` + `pull`/`cleanup`
  callbacks). The connection's send state machine pumps the producer in
  fixed-size chunks via chunked `Transfer-Encoding`, so large or generated
  responses never have to fit in RAM. The bundled file producer streams straight
  off disk; the same interface admits sockets, subprocesses, or generators.
- **Concurrency model.** Replaced the original fork-per-connection model with a
  single-threaded `poll()`-based event loop. Each connection carries an explicit
  phase enum (`CONN_READING_REQUEST → CONN_READING_BODY_* → CONN_BUILDING →
  CONN_SENDING_RESPONSE`) and a tagged-union arm holding the per-phase scratch;
  the dispatcher runs each connection forward until it would block, then yields
  back to the poll loop. No `fork()`, no threads. Portable to RTOS-class
  hardware.
