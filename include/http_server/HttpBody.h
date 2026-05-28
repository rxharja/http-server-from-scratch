//
// Created by redonxharja on 5/3/26.
//

#ifndef HTTPSERVER_HTTPBODY_H
#define HTTPSERVER_HTTPBODY_H
#include <stddef.h>
#include <sys/types.h>
#include "ParseResult.h"
#include "HttpBuffer.h"

#define MAX_BODY_LEN (1 * 1024 * 1000)
#define MAX_DECHUNK_SIZE (16 * 1024)
#define CHUNK_LINE_SIZE 64

typedef enum {
    TE_NONE,         // header absent
    TE_CHUNKED,      // chunked alone, or last in list
    TE_UNSUPPORTED,  // any non-chunked coding present
} TransferCoding;

typedef enum {
    CHUNK_SIZE,
    CHUNK_DATA,
    CHUNK_TRAIL_CR,
    CHUNK_TRAIL_LF,
    CHUNK_TRAILER,
    CHUNK_DONE
} ChunkedPhase;

typedef struct {
    ChunkedPhase phase;
    size_t remaining; // stores the chunk size
    // char line_buf[CHUNK_LINE_SIZE]; // these will be re-added when streaming is supported
    // size_t line_len;
} ChunkDecoder;

typedef struct {
    ParseResult parse_result;
    size_t bytes_written;
} ChunkResult;

typedef enum {
    READ_BODY_OK = 0,
    READ_BODY_PEER_CLOSED,
    READ_BODY_TOO_LARGE,
    READ_BODY_IO_ERROR,
    READ_BODY_BAD_DATA,
    READ_BODY_HAS_MORE
} ReadBodyStatus;

typedef struct {
    ReadBodyStatus status;
    size_t         next_req_offset;
} ReadBodyResult;

// Per-phase scratch for a Content-Length body read.
typedef struct { size_t received; } CLBodySt;

// Per-phase scratch for a chunked-Transfer-Encoding body read.
typedef struct {
    size_t       consumed;   // bytes already fed to dec, relative to body_start
    ChunkDecoder dec;
} ChunkedBodySt;

/**
 * Parse a Content-Length header value (base-10, capped at MAX_BODY_LEN).
 *
 * @param val  NUL-terminated header value (OWS-stripped by the header parser)
 * @param out  parsed length on PARSE_OK
 */
ParseStatus content_length_parse(const char * val, size_t * out);

/**
 * Parse a Transfer-Encoding header value. Two-pass: classifies every coding in
 * the list, then decides. See HttpBody.c for the matrix of OK / NOT_IMPLEMENTED
 * / BAD_REQUEST outcomes (chunked-position, duplicate-chunked, etc.).
 *
 * @param val     NUL-terminated header value (OWS-stripped by the header parser)
 * @param coding  resolved coding: TE_NONE / TE_CHUNKED / TE_UNSUPPORTED
 */
ParseStatus transfer_encoding_parse(const char * val, TransferCoding * coding);

/**
 * @param dec       Chunk Decoder containing the current phase and remaining bytes
 * @param in        pointer to next byte to consume
 * @param in_len    number of bytes readable starting at `in`
 * @param out       pointer to next byte position to write
 * @param out_avail number of bytes writable starting at `out`
 */
ChunkResult chunk_advance(ChunkDecoder * dec, const char * in, size_t in_len, char * out, size_t out_avail);

/**
 * Drain the Content-Length body from `fd` into `req`. Advances `st->received`
 * by however many bytes were read. Idempotent across poll wake-ups.
 *
 * @param fd          connection file descriptor (non-blocking)
 * @param req         request buffer; grown as bytes arrive
 * @param body_start  offset of body's first byte in `req->buffer`
 * @param body_len    expected body length (from Content-Length header)
 * @param st          in/out per-phase scratch (received-byte counter)
 */
ReadBodyResult conn_recv_body_cl(int fd, HttpBuffer * req, size_t body_start, size_t body_len, CLBodySt * st);

/**
 * Drain a chunked-Transfer-Encoding body. Pumps `chunk_advance` against the
 * wire-bytes window, accumulating decoded payload into `dechunked`. Idempotent
 * across poll wake-ups via `st->consumed` (raw cursor) and `st->dec` (decoder state).
 *
 * @param fd          connection file descriptor (non-blocking)
 * @param req_buf     request buffer; raw chunked bytes accumulate here
 * @param body_start  offset of body's first byte in `req_buf->buffer`
 * @param dechunked   destination for decoded payload bytes
 * @param st          in/out per-phase scratch (consumed cursor + decoder)
 */
ReadBodyResult conn_recv_body_chunked(int fd, HttpBuffer *req_buf, size_t body_start, HttpBuffer *dechunked, ChunkedBodySt * st);

#endif //HTTPSERVER_HTTPBODY_H