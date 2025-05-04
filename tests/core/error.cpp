#include "core/error.h"
#include "core/allocator.h"
#include <cstdio>
#include <gtest/gtest.h>
#include <string>
class TEST_error : public testing::Test {};

static void func2() {
  THROW("TestError", "this is a test error");
  return;
}

static void func1() {
  func2();
  CHECK_AND_THROW();
}

TEST_F(TEST_error, tostring) {
  noix_allocator_t allocator = noix_create_default_allocator();
  noix_error_initialize(allocator);
  func1();
  ASSERT_EQ(noix_has_error(), true);
  noix_error_t error = noix_poll_error(__FUNCTION__, __FILE__, __LINE__ - 2);
  char *message = noix_error_to_string(error);
  printf("%s\n", message);
  noix_allocator_free(allocator, error);
  ASSERT_TRUE(
      std::string(message).starts_with("TestError: this is a test error"));
  noix_allocator_free(allocator, message);
  noix_delete_allocator(allocator);
}