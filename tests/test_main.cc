/*
 * 单元测试入口（替代 gtest_main）。
 * 所有测试在执行前必须完成 ICU 数据引导（icu_data_init），
 * 否则 Unicode 属性查询（u_hasBinaryProperty 等）全部失效。
 */

#include <cstdio>

#include <gtest/gtest.h>

#include "icu_data.h"

int main(int argc, char **argv) {
  if (icu_data_init() != 0) {
    std::fprintf(stderr, "neojs_test: failed to initialize ICU data\n");
    return 1;
  }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
