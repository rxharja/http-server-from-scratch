//
// Created by redonxharja on 7/1/26.
//

#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include "http_server/Arena.h"

void arena_init(Arena * arena, void * mem, const size_t cap) {
   assert(arena);
   assert(mem);
   assert(cap > 0);

   arena->base = mem;
   arena->cap = cap;
   arena->used = 0;
}

size_t arena_mark(const Arena *arena) {
   assert(arena);
   assert(arena->used <= arena->cap);

   return arena->used;
}

void arena_reset_to(Arena * arena, const size_t mark) {
   assert(arena);
   assert(mark <= arena->used); // can only rewind not fast-forward

   arena->used = mark;

   assert(arena->used <= arena->cap);
}

void arena_reset(Arena * arena) {
   assert(arena);

   arena->used = 0;

   assert(arena->used == 0);
}

void * arena_alloc(Arena * arena, const size_t n, const size_t align) {
   assert(arena);
   assert(align > 0);
   assert((align & (align - 1)) == 0); // power of two, non-zero
   assert(arena->used <= arena->cap);

   // used 124
   // n 12
   // cap 256
   // align 128
   // round cursor up to alignment, to the align boundary. Valid when align is a power of 2
   const size_t aligned = (arena->used + (align -1)) & ~(align - 1);

   // check for OOM
   if (aligned > arena->cap || n > arena->cap - aligned) return NULL;

   // bump the base based on the aligned allocation and return to caller
   void * p = arena->base + aligned;
   arena->used = aligned + n;

   assert(((uintptr_t)p & (align - 1)) == 0); // ensure p is aligned
   assert(arena->used <= arena->cap); // ensure invariant is preserved, used is still below capacity

   return p;
}