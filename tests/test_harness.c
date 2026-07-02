//
// Test harness: defines the shared counters + check(), and the entry point
// that runs each file's test suite and prints the aggregate result.
//
#include "test_harness.h"
#include <stdio.h>

int total = 0, failed = 0;

void check(const char *label, const int ok, const char *detail) {
    total++;
    if (ok) {
        printf("ok   %s\n", label);
    } else {
        failed++;
        printf("FAIL %s: %s\n", label, detail);
    }
}

int main(void) {
    run_request_tests();
    run_body_tests();
    run_response_tests();
    run_stream_tests();
    run_arena_tests();

    printf("\n%d/%d passed\n", total - failed, total);
    return failed ? 1 : 0;
}
