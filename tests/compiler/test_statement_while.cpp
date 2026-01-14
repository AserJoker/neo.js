#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/ast_statement.h"
#include "neo.js/compiler/ast_statement_while.h"
#include "neo.js/compiler/scope.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_while : public neo_test {};
TEST_F(neo_test_statement_while, normal) {
  neo_location_t loc = create_location("while(test){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_WHILE);
  neo_ast_statement_while_t w =
      reinterpret_cast<neo_ast_statement_while_t>(node);
  ASSERT_NE(w->condition, nullptr);
  ASSERT_NE(w->body, nullptr);
  ASSERT_EQ(w->condition->type, NEO_NODE_TYPE_IDENTIFIER);
  ASSERT_EQ(w->body->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_while, comment) {
  neo_location_t loc = create_location(
      "while /*test\n*/ ( /*test\n*/ test /*test\n*/) /*test\n*/{}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_WHILE);
  neo_ast_statement_while_t w =
      reinterpret_cast<neo_ast_statement_while_t>(node);
  ASSERT_NE(w->condition, nullptr);
  ASSERT_NE(w->body, nullptr);
  ASSERT_EQ(w->condition->type, NEO_NODE_TYPE_IDENTIFIER);
  ASSERT_EQ(w->body->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_while, no_condition) {
  neo_location_t loc = create_location("while (){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_while, no_body) {
  neo_location_t loc = create_location("while (test)");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}