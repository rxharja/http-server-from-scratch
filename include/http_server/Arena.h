//
// Created by redonxharja on 7/1/26.
//

#ifndef HTTPSERVER_ARENA_H
#define HTTPSERVER_ARENA_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t * base; // start of backing region
    size_t cap; // total bytes available
    size_t used; // bytes currently used
} Arena;

void arena_init(Arena * arena, uint8_t * mem, size_t cap);

size_t arena_mark(const Arena *arena);

void arena_reset_to(Arena * arena, uint8_t mark);

void arena_reset(Arena * arena);

void * arena_alloc(Arena * arena, size_t n, size_t align);

#endif //HTTPSERVER_ARENA_H
