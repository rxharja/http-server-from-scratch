//
// Created by redonxharja on 7/1/26.
//

#include <stddef.h>
#include <stdint.h>
#include "http_server/Arena.h"

// convenience macros for common typed case so callers don't hand-write sizeof/_Alignof
#define arena_new(a, T)      ((T *)arena_alloc((a), sizeof(T), _Alignof(T)))
#define arena_array(a, T, k) ((T *)arena_alloc((a), sizeof(T) * (k), _Alignof(T)))

void arena_init(Arena * arena, uint8_t * mem, const size_t cap) {
   arena->base = mem;
   arena->cap = cap;
}

size_t arena_mark(const Arena *arena) {
   return arena->used;
}

void arena_reset_to(Arena * arena, const uint8_t mark) {
   arena->used = mark;
}

void arena_reset(Arena * arena) {
   arena->used = 0;
}

void * arena_alloc(Arena * arena, const size_t n, const size_t align) {
   // round cursor up to alignment, to a 4 byte boundary. Valid when align is a power of 2
   const size_t aligned = (arena->used + (align -1)) & ~(align - 1);

   // check for OOM
   if (aligned > arena->cap || n > arena->cap - aligned) return NULL;

   // bump the base based on the aligned allocation and return to caller
   void * p = arena->base + aligned;
   arena->used = aligned + n;
   return p;
}