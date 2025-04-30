#ifndef _H_TEST_UTIL_
#define _H_TEST_UTIL_

#include "config.h"
#include <stdio.h>
#include <string.h>
#define TEST(suit, test)                                                       \
  void TEST_##suit##_##test();                                                 \
  test_item_t TEST_##suit##_##test##_item = {#suit "." #test,                  \
                                             TEST_##suit##_##test};            \
  void TEST_##suit##_##test()

static inline char *TEST_Result() {
  static char result[1024] = "";
  return result;
}

#define ASSERT(expr)                                                           \
  if (!(expr)) {                                                               \
    sprintf(TEST_Result(), "expression:\n  %s\n at %s:%d\n", #expr, __FILE__,  \
            __LINE__);                                                         \
    return;                                                                    \
  }

static inline void RUN_TESTS(int argc, char *argv[]) {
  const char *suit = "core";
  const char *test = "allocator_alloc";
  test_item_t *tests[] = TESTS;
  for (int idx = 0; idx < argc; idx++) {
    if (strcmp(argv[idx], "--suit") == 0) {
      suit = argv[idx + 1];
      idx++;
    }
    if (strcmp(argv[idx], "--test") == 0) {
      test = argv[idx + 1];
      idx++;
    }
  }
  for (size_t idx = 0; tests[idx] != 0; idx++) {
    if (suit[0] != 0) {
      if (test[0] != 0) {
        char name[1024];
        sprintf(name, "%s.%s", suit, test);
        if (strcmp(tests[idx]->name, name) != 0) {
          continue;
        }
      } else {
        size_t offset = 0;
        for (; suit[offset] != 0; offset++) {
          if (tests[idx]->name[offset] != suit[offset]) {
            break;
          }
        }
        if (suit[offset] != 0) {
          continue;
        }
        if (tests[idx]->name[offset] != '.') {
          continue;
        }
      }
    }
    TEST_Result()[0] = 0;
    printf("\033[32m[ RUN    ]\033[m : %s\n", tests[idx]->name);
    tests[idx]->run();
    if (TEST_Result()[0] == 0) {
      printf("\033[32m[ OK     ]\033[m : %s\n", tests[idx]->name);
    } else {
      printf("\033[31m[ FAILED ]\033[m : %s\n  %s", tests[idx]->name,
             TEST_Result());
    }
  }
}
#endif