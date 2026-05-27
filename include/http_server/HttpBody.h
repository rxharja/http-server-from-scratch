//
// Created by redonxharja on 5/3/26.
//

#ifndef HTTPSERVER_HTTPBODY_H
#define HTTPSERVER_HTTPBODY_H
#include <stddef.h>
#include "ParseResult.h"

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

ParseStatus content_length_parse(const char * val, size_t * out);

ParseStatus transfer_encoding_parse(const char * val, TransferCoding * coding);

ChunkResult chunk_parse(const char * buf, const char * end, char * dest, size_t cap);

/**
 * @param dec       Chunk Decoder containing the current phase and remaining bytes
 * @param in        pointer to next byte to consume
 * @param in_len    number of bytes readable starting at `in`
 * @param out       pointer to next byte position to write
 * @param out_avail number of bytes writable starting at `out`
 */
ChunkResult chunk_advance(ChunkDecoder * dec, const char * in, size_t in_len, char * out, size_t out_avail);

ChunkResult body_dechunk(const char * buf, const char * end, char * dest, size_t cap);

#endif //HTTPSERVER_HTTPBODY_H