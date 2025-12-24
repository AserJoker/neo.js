#include "compiler/ast_literal_string.h"
#include "compiler/ast_node.h"
#include "compiler/scope.h"
#include "core/allocator.h"
#include "core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_string : public neo_test {};
neo_location_t create_location(const char *src);
TEST_F(neo_test_string, normal) {
  neo_location_t loc = create_location("\"abc\"");
  neo_ast_node_t node = neo_ast_read_literal_string(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_LITERAL_STRING);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_string, normal_single) {
  neo_location_t loc = create_location("'abc'");
  neo_ast_node_t node = neo_ast_read_literal_string(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_LITERAL_STRING);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_string, not_match) {
  neo_location_t loc = create_location("");
  neo_ast_node_t node = neo_ast_read_literal_string(allocator, "", &loc.end);
  ASSERT_EQ(node, nullptr);
}

TEST_F(neo_test_string, error) {
  neo_location_t loc = create_location("'aaaa");
  neo_ast_node_t node = neo_ast_read_literal_string(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}