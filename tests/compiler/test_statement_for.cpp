#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/ast_statement.h"
#include "neojs/compiler/ast_statement_for.h"
#include "neojs/compiler/scope.h"
#include "neojs/core/allocator.h"
#include "neojs/core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_for : public neo_test {};
TEST_F(neo_test_statement_for, normal) {
  neo_location_t loc = create_location("for(i=0;i<10;i++){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_FOR);
  auto st = reinterpret_cast<neo_ast_statement_for_t>(node);
  ASSERT_NE(st->initialize, nullptr);
  ASSERT_EQ(st->initialize->type, NEO_NODE_TYPE_EXPRESSION_ASSIGMENT);
  ASSERT_NE(st->condition, nullptr);
  ASSERT_EQ(st->condition->type, NEO_NODE_TYPE_EXPRESSION_BINARY);
  ASSERT_NE(st->after, nullptr);
  ASSERT_EQ(st->after->type, NEO_NODE_TYPE_EXPRESSION_BINARY);
  ASSERT_NE(st->body, nullptr);
  ASSERT_EQ(st->body->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_for, comment) {
  neo_location_t loc =
      create_location("for /*\n*/ ( /*\n*/ i=0 /*\n*/ ; /*\n*/ i<10 /*\n*/ ; "
                      "/*\n*/ i++ /*\n*/ ) /*\n*/ {}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_FOR);
  auto st = reinterpret_cast<neo_ast_statement_for_t>(node);
  ASSERT_NE(st->initialize, nullptr);
  ASSERT_EQ(st->initialize->type, NEO_NODE_TYPE_EXPRESSION_ASSIGMENT);
  ASSERT_NE(st->condition, nullptr);
  ASSERT_EQ(st->condition->type, NEO_NODE_TYPE_EXPRESSION_BINARY);
  ASSERT_NE(st->after, nullptr);
  ASSERT_EQ(st->after->type, NEO_NODE_TYPE_EXPRESSION_BINARY);
  ASSERT_NE(st->body, nullptr);
  ASSERT_EQ(st->body->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_for, no_init) {
  neo_location_t loc = create_location("for(;i<10;i++){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_FOR);
  auto st = reinterpret_cast<neo_ast_statement_for_t>(node);
  ASSERT_EQ(st->initialize, nullptr);
  ASSERT_NE(st->condition, nullptr);
  ASSERT_EQ(st->condition->type, NEO_NODE_TYPE_EXPRESSION_BINARY);
  ASSERT_NE(st->after, nullptr);
  ASSERT_EQ(st->after->type, NEO_NODE_TYPE_EXPRESSION_BINARY);
  ASSERT_NE(st->body, nullptr);
  ASSERT_EQ(st->body->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_for, no_condition) {
  neo_location_t loc = create_location("for(i=0;;i++){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_FOR);
  auto st = reinterpret_cast<neo_ast_statement_for_t>(node);
  ASSERT_NE(st->initialize, nullptr);
  ASSERT_EQ(st->initialize->type, NEO_NODE_TYPE_EXPRESSION_ASSIGMENT);
  ASSERT_EQ(st->condition, nullptr);
  ASSERT_NE(st->after, nullptr);
  ASSERT_EQ(st->after->type, NEO_NODE_TYPE_EXPRESSION_BINARY);
  ASSERT_NE(st->body, nullptr);
  ASSERT_EQ(st->body->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_for, no_after) {
  neo_location_t loc = create_location("for(i=0;i<10;){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_FOR);
  auto st = reinterpret_cast<neo_ast_statement_for_t>(node);
  ASSERT_NE(st->initialize, nullptr);
  ASSERT_EQ(st->initialize->type, NEO_NODE_TYPE_EXPRESSION_ASSIGMENT);
  ASSERT_NE(st->condition, nullptr);
  ASSERT_EQ(st->condition->type, NEO_NODE_TYPE_EXPRESSION_BINARY);
  ASSERT_EQ(st->after, nullptr);
  ASSERT_NE(st->body, nullptr);
  ASSERT_EQ(st->body->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_for, no_body) {
  neo_location_t loc = create_location("for(i=0;i<10;i++)");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_for, missing_semi) {
  neo_location_t loc = create_location("for(i<10;i++){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}