//
// Arena allocator tests. The arena is a bump allocator: alloc rounds the cursor
// up to the requested alignment, refuses to overrun the backing region, and only
// ever reclaims via reset/reset_to (never per-allocation). These tests drive the
// public API against a fixed, over-aligned backing buffer.
//
// Note: bad-contract inputs (align == 0, non-power-of-two align, reset_to a mark
// past `used`) are guarded by assert() in Arena.c, so they'd abort the process
// rather than return a value. They're contract violations, not behavior, so we
// don't exercise them here.
//
#include <stdint.h>
#include <string.h>
#include "http_server/Arena.h"
#include "test_harness.h"

// Backing region, over-aligned to 16 so a base-relative offset that's a multiple
// of N is also an N-aligned address (N <= 16). That lets us assert real pointer
// alignment, not just offset alignment.
#define BACKING_CAP 256
static _Alignas(16) uint8_t backing[BACKING_CAP];

static int is_aligned(const void *p, const size_t align) {
    return ((uintptr_t)p & (align - 1)) == 0;
}

void run_arena_tests(void) {
    // --- init establishes the invariant ---
    {
        Arena a;
        arena_init(&a, backing, BACKING_CAP);
        check("Arena init - base set",  a.base == backing, "base != backing");
        check("Arena init - cap set",   a.cap == BACKING_CAP, "cap mismatch");
        check("Arena init - used zero", a.used == 0, "used not zeroed");
        check("Arena init - mark zero", arena_mark(&a) == 0, "mark != 0 on fresh arena");
    }

    // --- basic alloc: returns a pointer inside the region and bumps the cursor ---
    {
        Arena a;
        arena_init(&a, backing, BACKING_CAP);
        void *p = arena_alloc(&a, 10, 1);
        check("Arena alloc - non-NULL",     p != NULL, "alloc returned NULL");
        check("Arena alloc - at base",      p == backing, "first alloc not at base (align 1)");
        check("Arena alloc - bumped used",  a.used == 10, "used != 10 after 10-byte alloc");
        // Second allocation is placed immediately after the first (align 1).
        void *q = arena_alloc(&a, 5, 1);
        check("Arena alloc - contiguous",   q == backing + 10, "second alloc not contiguous");
        check("Arena alloc - bumped again", a.used == 15, "used != 15 after 10+5");
    }

    // --- alignment: cursor rounds up, padding is consumed, pointer is aligned ---
    {
        Arena a;
        arena_init(&a, backing, BACKING_CAP);
        arena_alloc(&a, 1, 1);                 // used = 1 (deliberately mis-aligns the cursor)
        void *p = arena_alloc(&a, 4, 8);        // must round 1 up to 8
        check("Arena align - pointer aligned", is_aligned(p, 8), "returned pointer not 8-aligned");
        check("Arena align - at offset 8",     p == backing + 8, "not placed at aligned offset");
        check("Arena align - padding charged", a.used == 12, "used != 12 (8 aligned + 4 payload)");
    }

    // --- exact fit: an allocation equal to the remaining space succeeds ---
    {
        Arena a;
        arena_init(&a, backing, BACKING_CAP);
        void *p = arena_alloc(&a, BACKING_CAP, 1);
        check("Arena exact - fills region", p != NULL, "cap-sized alloc failed");
        check("Arena exact - used == cap",  a.used == BACKING_CAP, "used != cap after exact fill");
        // Nothing left: even a single byte must now fail.
        check("Arena exact - full then OOM", arena_alloc(&a, 1, 1) == NULL, "alloc past full succeeded");
    }

    // --- OOM boundary: cap+1 fails, and a failed alloc leaves the arena usable ---
    {
        Arena a;
        arena_init(&a, backing, BACKING_CAP);
        check("Arena OOM - over by one", arena_alloc(&a, BACKING_CAP + 1, 1) == NULL, "cap+1 alloc succeeded");
        check("Arena OOM - used intact", a.used == 0, "failed alloc mutated used");
        // The arena is still usable after an OOM: a fitting request still works.
        check("Arena OOM - recovers",    arena_alloc(&a, 16, 1) != NULL, "arena unusable after OOM");
    }

    // --- overflow: a huge n must not wrap the bounds check into a false success ---
    {
        Arena a;
        arena_init(&a, backing, BACKING_CAP);
        arena_alloc(&a, 8, 1);                  // move the cursor off zero
        check("Arena overflow - SIZE_MAX n NULL", arena_alloc(&a, SIZE_MAX, 1) == NULL, "SIZE_MAX alloc succeeded");
        check("Arena overflow - used intact",     a.used == 8, "overflow attempt mutated used");
    }

    // --- alignment near the end can overrun even when the payload alone fits ---
    {
        Arena a;
        arena_init(&a, backing, BACKING_CAP);
        // Fill to one byte short of cap, then ask for an aligned alloc: rounding the
        // cursor up pushes it past cap, so this must fail (the `aligned > cap` clause).
        arena_alloc(&a, BACKING_CAP - 1, 1);    // used = cap - 1
        check("Arena align-OOM - rounds past cap", arena_alloc(&a, 1, 16) == NULL, "aligned alloc overran region");
    }

    // --- mark / reset_to: reclaim per-request scratch, keep everything before it ---
    {
        Arena a;
        arena_init(&a, backing, BACKING_CAP);
        void *keep = arena_alloc(&a, 32, 1);    // "connection-lifetime" allocation
        const size_t mark = arena_mark(&a);
        check("Arena mark - value", mark == 32, "mark != used at checkpoint");

        void *scratch = arena_alloc(&a, 64, 1); // "per-request" scratch
        check("Arena mark - scratch placed", scratch == backing + 32, "scratch not after keep");
        check("Arena mark - grew",           a.used == 96, "used != 96 before reset");

        arena_reset_to(&a, mark);
        check("Arena reset_to - rewound", a.used == 32, "reset_to did not restore mark");

        // Next allocation reuses the reclaimed bytes: same address as the scratch.
        void *reused = arena_alloc(&a, 8, 1);
        check("Arena reset_to - reuses space", reused == scratch, "reclaimed bytes not reused");
        check("Arena reset_to - keep intact",  keep == backing, "pre-mark allocation moved");
    }

    // --- full reset: cursor returns to zero, next alloc lands at base ---
    {
        Arena a;
        arena_init(&a, backing, BACKING_CAP);
        arena_alloc(&a, 100, 1);
        arena_reset(&a);
        check("Arena reset - used zero",  a.used == 0, "reset did not zero used");
        check("Arena reset - realloc base", arena_alloc(&a, 4, 1) == backing, "post-reset alloc not at base");
    }

    // --- typed convenience macros ---
    {
        Arena a;
        arena_init(&a, backing, BACKING_CAP);
        uint32_t *one = arena_new(&a, uint32_t);
        check("Arena new - non-NULL", one != NULL, "arena_new returned NULL");
        check("Arena new - aligned",  is_aligned(one, _Alignof(uint32_t)), "arena_new result misaligned");

        uint32_t *arr = arena_array(&a, uint32_t, 4);
        check("Arena array - non-NULL", arr != NULL, "arena_array returned NULL");
        check("Arena array - aligned",  is_aligned(arr, _Alignof(uint32_t)), "arena_array result misaligned");
        // The two typed allocations must not overlap.
        check("Arena array - distinct", (void *)arr != (void *)one, "arena_array aliased arena_new");
    }
}
