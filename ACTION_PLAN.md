# Action Plan

Roadmap for taking the server from its current 0.9-ish state up through HTTP/2.
Sub-stages are sized to be roughly one focused session each.

## Bugs and structural cleanup (do first)

These bite at any HTTP version.

- [x] `.wasm` and `.js` Content-Type mappings added to `get_content_type`
- [x] Dead `.png` check fixed at `HttpRequest.c:161`
- [x] `parse_http_method` returns `PUT` for `"DELETE"` at `HttpRequest.c:16`
- [x] 404 reason phrase is `"File not Found"`; canonical is `Not Found`.
- [ ] Replace single `recv()` at `main.c:166` with a read loop that grows the buffer until `\r\n\r\n`, then up to `Content-Length` more bytes
- [ ] Replace `strtok` parser — mutates input, not reentrant, collapses repeated CRLF. Hand-roll a tokenizer that tracks positions.
- [ ] Header parser at `HttpRequest.c:51` assumes exactly `": "` after the colon. Spec allows zero-or-more OWS on either side.
- [ ] `free_response` at `HttpRequest.c:267` `free()`s cached `Content*` on cache hits. Add an `owns_content` flag, or always copy.
- [ ] `res->content = malloc(...)` at `HttpRequest.c:168` is overwritten by `dict_find` at line 181 — leak on every cache hit.
- [ ] `sendbuf` is a 100 KB stack array in `main.c`. Required for serving WASM payloads. Send headers and body directly to the socket in two `send()` calls instead of pre-serializing.
- [ ] No URL decoding; no path/query separation. `/foo%20bar?x=1` opens a literal file named that.
- [ ] No connection-level read timeout. Use `SO_RCVTIMEO` or `select`/`poll`.
- [ ] `trim_path` strips leading `.` and `/` only — does nothing for `..` segments mid-path. Resolve the path and reject anything escaping the document root.

## HTTP/0.9

Effectively done — you exceed strict 0.9 by always emitting a status line and headers, which is fine.

- [x] Accept `GET`
- [x] Handle zero request headers
- [x] Close connection after the body is written
- [ ] (Optional) Accept the bare `GET /path\r\n` form with no version token. Almost no client speaks this anymore — skip unless curious.

## HTTP/1.0

- [x] Parse `METHOD SP URI SP HTTP/1.x` request line
- [x] Parse zero+ headers terminated by an empty line
- [x] Emit `HTTP/x.y CODE REASON\r\n` status line
- [x] `GET`
- [x] `Content-Type` and `Content-Length` response headers
- [x] Close after each response
- [ ] `HEAD` (same headers as `GET`, no body)
- [ ] `POST` (currently 405) — even acknowledging with 200 and discarding the body is a starting point
- [ ] Read request body using `Content-Length`
- [ ] `Date` response header (RFC 7231 IMF-fixdate)
- [ ] `Server` response header
- [ ] `400 Bad Request` for malformed start lines / headers
- [ ] `501 Not Implemented` for unknown methods (separate from `405`)

## HTTP/1.1

The largest milestone. Five sub-stages.

### 1.1a — basics

- [x] Advertise `HTTP/1.1`
- [ ] Require `Host` header → `400` if missing
- [ ] Persistent connections by default; loop reading requests on the same fd
- [ ] Per-connection idle timeout and per-request header-read timeout
- [ ] Always emit `Date`
- [ ] Honor `Connection: close`

### 1.1b — bodies and framing

- [ ] Decode chunked requests (`chunk-size [;ext]\r\n data \r\n`, terminator `0\r\n\r\n`, optional trailers)
- [ ] Encode chunked responses when length isn't known up front
- [ ] `Expect: 100-continue` → emit `HTTP/1.1 100 Continue\r\n\r\n` before reading body, or reject with `417`
- [ ] Pipelining: queue and reply in arrival order
- [ ] Reject requests with both `Transfer-Encoding` and `Content-Length` (smuggling vector)

### 1.1c — caching and conditionals

- [ ] `Last-Modified` from `stat()`
- [ ] `ETag` (e.g. `"<size>-<mtime>"`)
- [ ] `If-Modified-Since` / `If-None-Match` → `304`
- [ ] `If-Match` / `If-Unmodified-Since` → `412`
- [ ] `Vary` when negotiating
- [ ] `Cache-Control` conditional on response (currently always `max-age=86400` for both 200 and 404)

### 1.1d — range requests

- [ ] Parse `Range: bytes=a-b` (single range first)
- [ ] `206 Partial Content` with `Content-Range`
- [ ] `If-Range`
- [ ] `416 Range Not Satisfiable`

### 1.1e — methods and status surface

- [ ] `OPTIONS` (and `OPTIONS *`) advertising via `Allow:`
- [ ] `PUT`, `DELETE` if you want write semantics
- [ ] `405` should include `Allow:`
- [ ] `408`, `413`, `414`, `415`

### 1.1f — concurrency rework

- [ ] Replace fork-per-request with one of: single-process `epoll`/`poll` event loop, pre-forked worker pool, or pthread-per-connection. Land this *before* 1.1b — persistent connections + fork-per-request gets ugly.

## HTTP/2

Separate wire protocol; build it as a parallel module sharing the request/response semantics.

### Refactor (prerequisite)

- [ ] Split `HttpRequest`/`HttpResponse` and handler logic into a transport-agnostic core. `parse_request` / `serialize_response` move into a 1.x wire module; an h2 module produces the same structs.

### Negotiation

- [ ] Pick `h2c` (cleartext upgrade from 1.1) or `h2` (TLS + ALPN). `h2c` is easier to learn from.
- [ ] Read the 24-byte connection preface
- [ ] Exchange `SETTINGS`, ack peer's settings

### Framing

- [ ] 9-byte frame header read/write loop
- [ ] `SETTINGS`, `PING`, `GOAWAY`, `WINDOW_UPDATE`
- [ ] `HEADERS`, `CONTINUATION`, `DATA`
- [ ] `RST_STREAM`, `PRIORITY`
- [ ] (Optional) `PUSH_PROMISE` — skip; rarely worthwhile
- [ ] Enforce `SETTINGS_MAX_FRAME_SIZE`
- [ ] Stream state machine: idle → open → half-closed → closed

### HPACK

- [ ] Static table (61 entries)
- [ ] Dynamic table with eviction
- [ ] Integer encoding with prefix scheme
- [ ] Huffman decoding (encoder optional)
- [ ] Lowercase header names; reject uppercase from peer
- [ ] Pseudo-headers `:method`/`:scheme`/`:authority`/`:path`/`:status`

### Flow control

- [ ] Per-stream and per-connection windows (default 65535)
- [ ] Send `WINDOW_UPDATE` as receive window drains
- [ ] Don't send `DATA` larger than `min(stream, connection)` window

### Multiplexing and shutdown

- [ ] Concurrent streams up to `SETTINGS_MAX_CONCURRENT_STREAMS`
- [ ] `GOAWAY` with last-processed stream id on shutdown
- [ ] Map errors to HTTP/2 codes (`PROTOCOL_ERROR`, `FLOW_CONTROL_ERROR`, `STREAM_CLOSED`, `FRAME_SIZE_ERROR`, `REFUSED_STREAM`, `CANCEL`, `COMPRESSION_ERROR`)

## Asteroids / WASM serving

- [x] `application/wasm` and `application/javascript` Content-Type mappings
- [ ] Lift the 100 KB response cap (see bugs section) — required for any non-trivial emscripten build
- [ ] Drop `index.html` + `*.js` + `*.wasm` (+ `*.data` if `--preload-file`) into the server's CWD
- [ ] (Optional) `Cross-Origin-Opener-Policy: same-origin` + `Cross-Origin-Embedder-Policy: require-corp` only if compiled with `-pthread`
- [ ] (Optional) gzip/brotli `Content-Encoding` — transfer-size win, not required
