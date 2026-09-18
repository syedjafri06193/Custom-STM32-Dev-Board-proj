/* A test harness small enough to read in one sitting.
 *
 * No framework dependency, because adding one to an embedded project means
 * either vendoring it or making the build need the network, and neither is
 * worth it for what amounts to thirty lines of macros.
 */

#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int tests_run;
extern int tests_failed;
extern const char *current_test;

#define TEST(name) static void name(void)

#define RUN(fn)                        \
    do {                               \
        current_test = #fn;            \
        tests_run++;                   \
        int before = tests_failed;     \
        fn();                          \
        if (tests_failed == before) {  \
            printf("  ok    %s\n", #fn); \
        }                              \
    } while (0)

#define FAIL_AT(fmt, ...)                                            \
    do {                                                             \
        tests_failed++;                                              \
        printf("  FAIL  %s\n        %s:%d: " fmt "\n", current_test, \
               __FILE__, __LINE__, ##__VA_ARGS__);                    \
    } while (0)

#define CHECK(cond)                             \
    do {                                        \
        if (!(cond)) {                          \
            FAIL_AT("expected: %s", #cond);     \
            return;                             \
        }                                       \
    } while (0)

#define CHECK_MSG(cond, msg)                              \
    do {                                                  \
        if (!(cond)) {                                    \
            FAIL_AT("%s (%s)", msg, #cond);               \
            return;                                       \
        }                                                 \
    } while (0)

#define CHECK_EQ(got, want)                                                \
    do {                                                                   \
        long long g_ = (long long)(got), w_ = (long long)(want);           \
        if (g_ != w_) {                                                    \
            FAIL_AT("%s: got %lld, want %lld", #got, g_, w_);              \
            return;                                                        \
        }                                                                  \
    } while (0)

#define CHECK_NEAR(got, want, tol)                                          \
    do {                                                                    \
        long long g_ = (long long)(got), w_ = (long long)(want);            \
        long long d_ = g_ > w_ ? g_ - w_ : w_ - g_;                         \
        if (d_ > (long long)(tol)) {                                        \
            FAIL_AT("%s: got %lld, want %lld +/- %lld (off by %lld)", #got, \
                    g_, w_, (long long)(tol), d_);                          \
            return;                                                         \
        }                                                                   \
    } while (0)

#define CHECK_STR(got, want)                                          \
    do {                                                              \
        if (strcmp((got), (want)) != 0) {                             \
            FAIL_AT("%s: got \"%s\", want \"%s\"", #got, (got), (want)); \
            return;                                                   \
        }                                                             \
    } while (0)

#define TEST_MAIN(suite_name, body)                                  \
    int tests_run = 0;                                               \
    int tests_failed = 0;                                            \
    const char *current_test = "";                                   \
    int main(void) {                                                 \
        printf("%s\n", suite_name);                                  \
        body;                                                        \
        printf("%s: %d tests, %d failed\n\n", suite_name, tests_run, \
               tests_failed);                                        \
        return tests_failed == 0 ? 0 : 1;                            \
    }

#endif /* TEST_H */
