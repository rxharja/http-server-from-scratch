//
// Created by redonxharja on 5/3/26.
//

#ifndef HTTPSERVER_HTTPBODY_H
#define HTTPSERVER_HTTPBODY_H
#include <stddef.h>
#include "ParseResult.h"

#define MAX_BODY_LEN (1 * 1024 * 1000)

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
    size_t remaining;
    char line_buf[64];
    size_t line_len;
} ChunkDecoder;

typedef struct {
    ParseResult parse_result;
    size_t chunk_size;
} ChunkResult;

ParseStatus parse_content_length(const char * val, size_t * out);

ParseStatus parse_transfer_encoding(const char * val, TransferCoding * coding);

ChunkResult parse_chunk(const char * buf, const char * end, char * dest, size_t cap);

ChunkResult body_dechunk(const char * buf, const char * end, char * dest, size_t cap);

#endif //HTTPSERVER_HTTPBODY_H