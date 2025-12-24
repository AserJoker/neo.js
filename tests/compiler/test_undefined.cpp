#include "compiler/ast_literal_undefined.h"
#include "compiler/ast_node.h"
#include "compiler/scope.h"
#include "core/allocator.h"
#include "core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_undefined : public neo_test {};
neo_location_t create_location(const char *src);
TEST_F(neo_test_undefined, normal) {
  neo_location_t loc = create_location("undefined");
  neo_ast_node_t node = neo_ast_read_literal_undefined(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_LITERAL_UNDEFINED);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_undefined, not_match) {
  neo_location_t loc = create_location("");
  neo_ast_node_t node = neo_ast_read_literal_undefined(allocator, "", &loc.end);
  ASSERT_EQ(node, nullptr);
}

TEST_F(neo_test_undefined, error) {
  neo_location_t loc = create_location("abc#");
  neo_ast_node_t node = neo_ast_read_literal_undefined(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}