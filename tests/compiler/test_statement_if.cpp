#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/ast_statement.h"
#include "neojs/compiler/ast_statement_if.h"
#include "neojs/core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_if : public neo_test {};
TEST_F(neo_test_statement_if, no_else) {
  neo_location_t loc = create_location("if(a){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_IF);
  auto st = reinterpret_cast<neo_ast_statement_if_t>(node);
  ASSERT_NE(st->alternate, nullptr);
  ASSERT_EQ(st->alternate->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  ASSERT_NE(st->condition, nullptr);
  ASSERT_EQ(st->condition->type, NEO_NODE_TYPE_IDENTIFIER);
  ASSERT_EQ(st->consequent, nullptr);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_if, with_else) {
  neo_location_t loc = create_location("if(a){}else{}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_IF);
  auto st = reinterpret_cast<neo_ast_statement_if_t>(node);
  ASSERT_NE(st->alternate, nullptr);
  ASSERT_EQ(st->alternate->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  ASSERT_NE(st->condition, nullptr);
  ASSERT_EQ(st->condition->type, NEO_NODE_TYPE_IDENTIFIER);
  ASSERT_NE(st->consequent, nullptr);
  ASSERT_EQ(st->consequent->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_if, comment) {
  neo_location_t loc = create_location(
      "if /*\n*/ ( /*\n*/ a /*\n*/ ) /*\n*/ {} /*\n*/ else /*\n*/ {}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_IF);
  auto st = reinterpret_cast<neo_ast_statement_if_t>(node);
  ASSERT_NE(st->alternate, nullptr);
  ASSERT_EQ(st->alternate->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  ASSERT_NE(st->condition, nullptr);
  ASSERT_EQ(st->condition->type, NEO_NODE_TYPE_IDENTIFIER);
  ASSERT_NE(st->consequent, nullptr);
  ASSERT_EQ(st->consequent->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_if, no_condition) {
  neo_location_t loc = create_location("if(){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_if, no_alt) {
  neo_location_t loc = create_location("if(a)");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_if, no_con) {
  neo_location_t loc = create_location("if(a){}else");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}