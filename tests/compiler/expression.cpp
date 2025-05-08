#include "compiler/node.h"
#include "compiler/parser.h"
#include "core/allocator.h"
#include "core/error.h"
#include <gtest/gtest.h>

class TEST_expression : public testing::Test {
protected:
  neo_allocator_t _allocator;

public:
  void SetUp() override {
    _allocator = neo_create_default_allocator();
    neo_error_initialize(_allocator);
  }

  void TearDown() override {
    if (neo_has_error()) {
      neo_error_t error = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
      if (error) {
        char *msg = neo_error_to_string(error);
        std::cerr << msg << std::endl;
        neo_allocator_free(_allocator, msg);
        neo_allocator_free(_allocator, error);
      }
    }
    neo_delete_allocator(_allocator);
    _allocator = NULL;
  }
};
TEST_F(TEST_expression, add) {
  const char *str = "'use strict';\r\n'use client'";
  neo_ast_node_t node = neo_ast_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(neo_has_error());
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  neo_allocator_free(_allocator, node);
}
TEST_F(TEST_expression, exp14) {
  const char *str = "!!+'hello'";
  neo_ast_node_t node = neo_ast_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(neo_has_error());
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  neo_allocator_free(_allocator, node);
}