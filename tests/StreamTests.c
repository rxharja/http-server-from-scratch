//
// File-producer streaming tests: the file producer (file_stream_open/file_pull/
// file_close) is file-private in HttpRouter.c, so we drive it through the public
// response_for_entry() door with a SERVE_DYN_STREAMED entry, then pump the
// returned pull()/cleanup() against real temp files.
//
// These cover what a static analyzer can't: pull() returns the file's bytes
// across multiple calls (the kernel file offset is stateful), returns 0 exactly
// at EOF (idempotently), handles an empty file, and an open failure degrades to
// a buffered error rather than a stream. cleanup() is invoked on every success
// path, so the suite also exercises the close()+free() release.
//
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include "http_server/HttpResponse.h"
#include "http_server/HttpRouter.h"
#include "http_server/Arena.h"
#include "test_harness.h"

// The streamed dispatch path now arena-allocates its FileCtx from req->scratch,
// so tests must hand response_for_entry a request carrying a live scratch arena
// (a FileCtx is only a few bytes). The request is otherwise unused on this path.
// Statics keep the ~13 KiB HttpRequest off the test stack; each call re-inits the
// arena, so a stream's ctx stays valid until the caller drains and cleans it up.
static HttpRequest *stream_req(void) {
    static uint8_t    scratch_mem[256];
    static Arena      scratch;
    static HttpRequest req;
    arena_init(&scratch, scratch_mem, sizeof scratch_mem);
    req.scratch = &scratch;
    return &req;
}

// serve_file is the registration API but isn't in the public header yet, so we
// forward-declare it here to drive the real serve_file -> lookup -> dispatch path
// until it's exported. (It's a non-static symbol, so this links against the lib.)
int content_registry_add_file(ContentRegistry * cache, const char * fs_path, const char * url, ServeMode mode);

// True if any of the first `n` headers carries `key`.
static int has_header(const ResponseHeader *h, const size_t n, const char *key) {
    for (size_t i = 0; i < n; i++) if (strcmp(h[i].key, key) == 0) return 1;
    return 0;
}

// Value of the first header matching `key` among the first `n`, or NULL.
static const char *header_value(const ResponseHeader *h, const size_t n, const char *key) {
    for (size_t i = 0; i < n; i++) if (strcmp(h[i].key, key) == 0) return h[i].value;
    return NULL;
}

// Write `content` to a fresh temp file; fills path_out (>= 32 bytes) with its
// name. The producer opens its own fd, so we close ours here. Returns 0 on ok.
static int make_temp(char *path_out, const char *content, const size_t len) {
    strcpy(path_out, "/tmp/httpsrv_stream_XXXXXX");
    const int fd = mkstemp(path_out);
    if (fd < 0) return -1;
    if (len > 0 && write(fd, content, len) != (ssize_t)len) { close(fd); unlink(path_out); return -1; }
    close(fd);
    return 0;
}

// Build a SERVE_DYN_STREAMED entry pointing at fs_path and dispatch through the
// public door with a scratch-carrying request (see stream_req).
static HttpResponse open_stream(RevalMeta *meta, ContentEntry *entry, const char *fs_path) {
    *meta = (RevalMeta){0};
    strncpy(meta->fs_path, fs_path, sizeof(meta->fs_path) - 1);
    *entry = (ContentEntry){0};
    entry->mode = SERVE_DYN_STREAMED;
    entry->reval = meta;
    return response_for_entry(stream_req(), entry);
}

// Pull the whole stream into out using reads of at most `chunk` bytes.
// Returns total bytes at EOF, -1 on a pull error, -2 on output overflow.
static ssize_t drain(const Stream *s, char *out, const size_t out_cap, const size_t chunk) {
    size_t total = 0;
    for (;;) {
        if (total >= out_cap) return -2;               // would force a 0-length read
        size_t want = chunk;
        if (want > out_cap - total) want = out_cap - total;
        const ssize_t n = s->pull(s->ctx, out + total, want);
        if (n < 0) return -1;
        if (n == 0) return (ssize_t)total;             // EOF
        total += (size_t)n;
    }
}

void run_stream_tests(void) {
    const char *body = "hello world, streamed in chunks";
    const size_t blen = strlen(body);
    char path[64];
    char out[256];

    // --- happy path: one big pull returns all bytes, then EOF ---
    {
        if (make_temp(path, body, blen) != 0) {
            check("Stream prod - temp file", 0, "make_temp failed");
        } else {
            RevalMeta meta; ContentEntry entry;
            HttpResponse res = open_stream(&meta, &entry, path);
            check("Stream prod - kind is stream", res.kind == BODY_STREAM, "expected BODY_STREAM");
            check("Stream prod - pull set",       res.body.stream.pull != NULL, "pull is NULL");
            check("Stream prod - cleanup set",    res.body.stream.cleanup != NULL, "cleanup is NULL");
            if (res.kind == BODY_STREAM && res.body.stream.pull) {
                const ssize_t n = drain(&res.body.stream, out, sizeof(out), sizeof(out));
                check("Stream prod - full length", n == (ssize_t)blen, "byte count mismatch");
                check("Stream prod - bytes match",
                      n == (ssize_t)blen && memcmp(out, body, blen) == 0, "content mismatch");
                // EOF is idempotent: pulling past the end still returns 0.
                check("Stream prod - EOF idempotent",
                      res.body.stream.pull(res.body.stream.ctx, out, sizeof(out)) == 0, "post-EOF pull not 0");
                res.body.stream.cleanup(res.body.stream.ctx);
            }
            unlink(path);
        }
    }

    // --- chunked: tiny reads still reassemble the file (offset is stateful) ---
    {
        if (make_temp(path, body, blen) == 0) {
            RevalMeta meta; ContentEntry entry;
            HttpResponse res = open_stream(&meta, &entry, path);
            if (res.kind == BODY_STREAM && res.body.stream.pull) {
                const ssize_t n = drain(&res.body.stream, out, sizeof(out), 3); // 3-byte reads
                check("Stream prod - chunked length",
                      n == (ssize_t)blen, "byte count mismatch under small reads");
                check("Stream prod - chunked bytes",
                      n == (ssize_t)blen && memcmp(out, body, blen) == 0, "content mismatch under small reads");
                res.body.stream.cleanup(res.body.stream.ctx);
            } else {
                check("Stream prod - chunked open", 0, "did not get a stream");
            }
            unlink(path);
        }
    }

    // --- empty file: first pull is immediately EOF (0) ---
    {
        if (make_temp(path, "", 0) == 0) {
            RevalMeta meta; ContentEntry entry;
            HttpResponse res = open_stream(&meta, &entry, path);
            if (res.kind == BODY_STREAM && res.body.stream.pull) {
                check("Stream prod - empty file EOF",
                      res.body.stream.pull(res.body.stream.ctx, out, sizeof(out)) == 0,
                      "empty file did not return 0");
                res.body.stream.cleanup(res.body.stream.ctx);
            } else {
                check("Stream prod - empty open", 0, "did not get a stream");
            }
            unlink(path);
        }
    }

    // --- open failure: a missing file degrades to a buffered 500, not a stream ---
    {
        RevalMeta meta; ContentEntry entry;
        HttpResponse res = open_stream(&meta, &entry, "/tmp/httpsrv_stream_does_not_exist_999999");
        check("Stream prod - missing file not stream", res.kind != BODY_STREAM, "open failure produced a stream");
        check("Stream prod - missing file is 500",     res.status == 500, "open failure not a 500");
    }

    // === end-to-end: serve_file registration -> lookup -> response_for_entry ===
    // The blocks above hand-build a ContentEntry; here we exercise the real
    // registration path, so the 4-header / no-Content-Length streamed layout is
    // verified exactly as serve_file writes it (a smuggling guard) and the entry
    // reaches a BODY_STREAM response through the public dispatcher.
    {
        if (make_temp(path, body, blen) == 0) {
            ContentRegistry *reg = content_registry_create();
            const int ok = content_registry_add_file(reg, path, "/stream.bin", SERVE_DYN_STREAMED);
            check("Serve file - registered", ok == 1, "serve_file did not return 1");

            // req is unused on the streamed lookup path, so NULL is safe.
            const ContentLookupResult d = content_registry_lookup(reg, NULL, "/stream.bin");
            check("Serve file - lookup hit",    d.status == CONTENT_HIT, "expected CONTENT_HIT");
            check("Serve file - entry present", d.entry != NULL, "entry is NULL");

            if (d.entry) {
                check("Serve file - mode streamed",
                      d.entry->mode == SERVE_DYN_STREAMED, "mode is not SERVE_DYN_STREAMED");
                check("Serve file - fs_path set",
                      d.entry->reval && strcmp(d.entry->reval->fs_path, path) == 0,
                      "reval->fs_path is not the source path");
                // Streamed entries are chunked, so they must carry NO Content-Length.
                check("Serve file - entry has no content-length",
                      !has_header(d.entry->headers, 4, "Content-Length"),
                      "registered entry headers contain Content-Length");

                HttpResponse res = response_for_entry(stream_req(), d.entry);
                check("Serve file - dispatch is stream",
                      res.kind == BODY_STREAM, "response_for_entry not BODY_STREAM");
                check("Serve file - header count 4",
                      res.header_count == 4, "streamed header_count != 4");
                check("Serve file - response has no content-length",
                      !has_header(res.headers, res.header_count, "Content-Length"),
                      "streamed response headers contain Content-Length");
                if (res.kind == BODY_STREAM && res.body.stream.pull) {
                    const ssize_t n = drain(&res.body.stream, out, sizeof(out), sizeof(out));
                    check("Serve file - body matches",
                          n == (ssize_t)blen && memcmp(out, body, blen) == 0,
                          "streamed body did not match source");
                    res.body.stream.cleanup(res.body.stream.ctx);
                }
            }

            content_registry_free(reg); // frees the entry; producer ctx is independent
            unlink(path);
        }
    }

    // === static-streamed: stream off disk with an immutable cache header ===
    // The fourth quadrant of the freshness x delivery matrix. Delivery is
    // identical to SERVE_DYN_STREAMED (BODY_STREAM, no Content-Length); the only
    // difference is the freshness policy, so the guard is that Cache-Control says
    // "immutable" here where the dynamic mode says "no-cache".
    {
        if (make_temp(path, body, blen) == 0) {
            ContentRegistry *reg = content_registry_create();
            const int ok = content_registry_add_file(reg, path, "/asset.bin", SERVE_STATIC_STREAMED);
            check("Static stream - registered", ok == 1, "add_file did not return 1");

            const ContentLookupResult d = content_registry_lookup(reg, NULL, "/asset.bin");
            check("Static stream - lookup hit", d.status == CONTENT_HIT, "expected CONTENT_HIT");

            if (d.entry) {
                check("Static stream - mode",
                      d.entry->mode == SERVE_STATIC_STREAMED, "mode is not SERVE_STATIC_STREAMED");
                const char *cc = header_value(d.entry->headers, 4, "Cache-Control");
                check("Static stream - immutable cache header",
                      cc && strcmp(cc, "max-age=31536000, immutable") == 0,
                      "Cache-Control is not the immutable policy");
                check("Static stream - no content-length",
                      !has_header(d.entry->headers, 4, "Content-Length"),
                      "static-streamed entry carries Content-Length");

                HttpResponse res = response_for_entry(stream_req(), d.entry);
                check("Static stream - dispatch is stream",
                      res.kind == BODY_STREAM, "response_for_entry not BODY_STREAM");
                if (res.kind == BODY_STREAM && res.body.stream.pull) {
                    const ssize_t n = drain(&res.body.stream, out, sizeof(out), sizeof(out));
                    check("Static stream - body matches",
                          n == (ssize_t)blen && memcmp(out, body, blen) == 0,
                          "streamed body did not match source");
                    res.body.stream.cleanup(res.body.stream.ctx);
                }
            }

            content_registry_free(reg);
            unlink(path);
        }
    }
}
