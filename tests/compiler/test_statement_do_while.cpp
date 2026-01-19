#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/ast_statement.h"
#include "neojs/compiler/ast_statement_do_while.h"
#include "test.hpp"
class neo_test_statement_do_while : public neo_test {};
TEST_F(neo_test_statement_do_while, normal) {
  neo_location_t loc = create_location("do{}while(test)");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_DO_WHILE);
  auto w = reinterpret_cast<neo_ast_statement_do_while_t>(node);
  ASSERT_NE(w->condition, nullptr);
  ASSERT_NE(w->body, nullptr);
  ASSERT_EQ(w->condition->type, NEO_NODE_TYPE_IDENTIFIER);
  ASSERT_EQ(w->body->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_do_while, asi) {
  neo_location_t loc = create_location("do{}while(test) a = 1");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_DO_WHILE);
  auto w = reinterpret_cast<neo_ast_statement_do_while_t>(node);
  ASSERT_NE(w->condition, nullptr);
  ASSERT_NE(w->body, nullptr);
  ASSERT_EQ(w->condition->type, NEO_NODE_TYPE_IDENTIFIER);
  ASSERT_EQ(w->body->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_do_while, comment) {
  neo_location_t loc = create_location(
      "do /*test\n*/{}/*test\n*/while/*test\n*/(/*test\n*/test/*test\n*/)");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_DO_WHILE);
  auto w = reinterpret_cast<neo_ast_statement_do_while_t>(node);
  ASSERT_NE(w->condition, nullptr);
  ASSERT_NE(w->body, nullptr);
  ASSERT_EQ(w->condition->type, NEO_NODE_TYPE_IDENTIFIER);
  ASSERT_EQ(w->body->type, NEO_NODE_TYPE_STATEMENT_BLOCK);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_do_while, no_condition) {
  neo_location_t loc = create_location("do{}while()");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_do_while, no_pair_brackets) {
  neo_location_t loc = create_location("do{}while(aaaa");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_do_while, no_body) {
  neo_location_t loc = create_location("do while(aaaa)");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}