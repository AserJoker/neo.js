#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include <gtest/gtest.h>
#include <iostream>
#include <string>

class TEST_token : public testing::Test {
protected:
  noix_allocator_t _allocator;

public:
  void SetUp() override {
    _allocator = noix_create_default_allocator();
    noix_error_initialize(_allocator);
  }

  void TearDown() override {
    if (noix_has_error()) {
      noix_error_t error = noix_poll_error(__FUNCTION__, __FILE__, __LINE__);
      if (error) {
        char *msg = noix_error_to_string(error);
        std::cerr << msg << std::endl;
        noix_allocator_free(_allocator, msg);
        noix_allocator_free(_allocator, error);
      }
    }
    noix_delete_allocator(_allocator);
    _allocator = NULL;
  }
};

TEST_F(TEST_token, read_string_token) {
  const char *str = "'hello world \\u{211e}'";
  noix_position_t position;
  position.line = 1;
  position.column = 1;
  position.offset = str;
  noix_token_t token = noix_read_string_token(_allocator, "test.js", &position);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(token != NULL);
  ASSERT_EQ(token->type, NOIX_TOKEN_TYPE_STRING);
  ASSERT_EQ(
      std::string(token->position.begin.offset, token->position.end.offset),
      str);
  noix_allocator_free(_allocator, token);
}