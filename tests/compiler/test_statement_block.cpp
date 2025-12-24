#include "compiler/ast_node.h"
#include "compiler/ast_statement.h"
#include "compiler/scope.h"
#include "core/allocator.h"
#include "core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_block : public neo_test {};
neo_location_t create_location(const char *src);

TEST_F(neo_test_statement_block, normal) {
  neo_location_t loc = create_location("{}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_block, error) {
  neo_location_t loc = create_location("{");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_block, comment1) {
  neo_location_t loc = create_location("{/**/}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_block, comment2) {
  neo_location_t loc = create_location("{}/**/");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}