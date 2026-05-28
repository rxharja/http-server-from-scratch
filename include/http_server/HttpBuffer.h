//
// Created by redonxharja on 5/28/26.
//

#ifndef HTTPSERVER_HTTPBUFFER_H
#define HTTPSERVER_HTTPBUFFER_H
#include <stddef.h>

/**
 * Simple owning byte buffer. `buffer` points at storage of size `cap`;
 * `size` is the live byte count (0..cap). Storage ownership is the caller's.
 */
typedef struct {
    size_t cap;
    size_t size;
    char * buffer;
} HttpBuffer;

#endif //HTTPSERVER_HTTPBUFFER_H
