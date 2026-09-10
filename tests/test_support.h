/* Tiny assertion helpers for the host suites. Each suite is one executable
 * that prints a line per check and exits non-zero if any failed, so `make`
 * fails loudly and the output says which expectation broke. */
#ifndef TESTS_TEST_SUPPORT_H
#define TESTS_TEST_SUPPORT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_failed;
static int tests_run;

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++tests_run;                                                               \
    if (!(cond)) {                                                             \
      ++tests_failed;                                                          \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                   \
    }                                                                          \
  } while (0)

#define CHECK_EQ(actual, expected)                                             \
  do {                                                                         \
    ++tests_run;                                                               \
    long a_ = (long)(actual), e_ = (long)(expected);                           \
    if (a_ != e_) {                                                            \
      ++tests_failed;                                                          \
      printf("FAIL %s:%d  %s == %ld, expected %ld\n", __FILE__, __LINE__,      \
             #actual, a_, e_);                                                 \
    }                                                                          \
  } while (0)

#define CHECK_STR_EQ(actual, expected)                                         \
  do {                                                                         \
    ++tests_run;                                                               \
    if (strcmp((actual), (expected)) != 0) {                                   \
      ++tests_failed;                                                          \
      printf("FAIL %s:%d  %s ==\n  \"%s\"\nexpected\n  \"%s\"\n", __FILE__,    \
             __LINE__, #actual, (actual), (expected));                         \
    }                                                                          \
  } while (0)

#define TESTS_REPORT(suite)                                                    \
  do {                                                                         \
    printf("%-20s %d checks, %d failed\n", (suite), tests_run, tests_failed);  \
    return tests_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;                    \
  } while (0)

#endif /* TESTS_TEST_SUPPORT_H */
