#include "neo.js/compiler/ast_literal_null.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/scope.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_null : public neo_test {};
neo_location_t create_location(const char *src);
TEST_F(neo_test_null, normal) {
  neo_location_t loc = create_location("null");
  neo_ast_node_t node = neo_ast_read_literal_null(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_LITERAL_NULL);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_null, not_match) {
  neo_location_t loc = create_location("");
  neo_ast_node_t node = neo_ast_read_literal_null(allocator, "", &loc.end);
  ASSERT_EQ(node, nullptr);
}

TEST_F(neo_test_null, error) {
  neo_location_t loc = create_location("abc#");
  neo_ast_node_t node = neo_ast_read_literal_null(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}