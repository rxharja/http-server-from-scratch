//
// Shared test harness: pass/fail counters + check(). Each *Tests.c file defines
// a run_*_tests() entry point; main() in test_harness.c calls them and reports.
//
#ifndef HTTPSERVER_TEST_HARNESS_H
#define HTTPSERVER_TEST_HARNESS_H

extern int total, failed;

void check(const char *label, int ok, const char *detail);

// Per-file entry points, defined in their respective translation units.
void run_request_tests(void);
void run_body_tests(void);

#endif //HTTPSERVER_TEST_HARNESS_H
