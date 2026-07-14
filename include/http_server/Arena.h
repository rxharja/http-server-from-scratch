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

void arena_init(Arena * arena, void * mem, size_t cap);

size_t arena_mark(const Arena *arena);

void arena_reset_to(Arena * arena, size_t mark);

void arena_reset(Arena * arena);

// returns uninitialized memory, caller must initialize
void * arena_alloc(Arena * arena, size_t n, size_t align);

// convenience macros for common typed case so callers don't hand-write sizeof/_Alignof
#define arena_new(a, T)      ((T *)arena_alloc((a), sizeof(T), _Alignof(T)))

#define arena_array(a, T, k) ((T *)arena_alloc((a), sizeof(T) * (k), _Alignof(T)))

#endif //HTTPSERVER_ARENA_H
