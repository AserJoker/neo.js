#include "compiler/ast_node.h"
#include "compiler/ast_statement.h"
#include "compiler/ast_statement_labeled.h"
#include "core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_labeled : public neo_test {};
TEST_F(neo_test_statement_labeled, normal) {
  neo_location_t loc = create_location("test:{}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_LABELED);
  neo_ast_statement_labeled_t labeled =
      reinterpret_cast<neo_ast_statement_labeled_t>(node);
  ASSERT_NE(labeled->statement, nullptr);
  ASSERT_EQ(labeled->statement->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  ASSERT_NE(labeled->label, nullptr);
  ASSERT_EQ(labeled->label->type, NEO_NODE_TYPE_IDENTIFIER);
  char *s = neo_location_get_raw(allocator, labeled->label->location);
  ASSERT_EQ(std::string(s), "test");
  neo_allocator_free(allocator, s);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_labeled, comment) {
  neo_location_t loc = create_location("test //comment\n ://comment\n{}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_LABELED);
  neo_ast_statement_labeled_t labeled =
      reinterpret_cast<neo_ast_statement_labeled_t>(node);
  ASSERT_NE(labeled->statement, nullptr);
  ASSERT_EQ(labeled->statement->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  ASSERT_NE(labeled->label, nullptr);
  ASSERT_EQ(labeled->label->type, NEO_NODE_TYPE_IDENTIFIER);
  char *s = neo_location_get_raw(allocator, labeled->label->location);
  ASSERT_EQ(std::string(s), "test");
  neo_allocator_free(allocator, s);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_labeled, missing_statement) {
  neo_location_t loc = create_location("test :");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}