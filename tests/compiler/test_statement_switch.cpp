#include "compiler/ast_node.h"
#include "compiler/ast_statement.h"
#include "compiler/ast_statement_switch.h"
#include "compiler/ast_switch_case.h"
#include "core/allocator.h"
#include "core/list.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_switch : public neo_test {};
TEST_F(neo_test_statement_switch, empty_body) {
  neo_location_t loc = create_location("switch (a){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_SWITCH);
  auto st = reinterpret_cast<neo_ast_statement_switch_t>(node);
  ASSERT_NE(st->condition, nullptr);
  ASSERT_EQ(st->condition->type, NEO_NODE_TYPE_IDENTIFIER);
  ASSERT_EQ(neo_list_get_size(st->cases), 0);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_switch, one_case) {
  neo_location_t loc = create_location("switch (a){ case 1:}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_SWITCH);
  auto st = reinterpret_cast<neo_ast_statement_switch_t>(node);
  ASSERT_EQ(neo_list_get_size(st->cases), 1);
  auto item = neo_list_get_first(st->cases);
  auto cas = reinterpret_cast<neo_ast_switch_case_t>(neo_list_node_get(item));
  ASSERT_NE(cas->condition, nullptr);
  ASSERT_EQ(cas->condition->type, NEO_NODE_TYPE_LITERAL_NUMERIC);
  ASSERT_NE(cas->body, nullptr);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_switch, multi_case) {
  neo_location_t loc = create_location("switch (a){ case 1:case 2:}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_SWITCH);
  auto st = reinterpret_cast<neo_ast_statement_switch_t>(node);
  ASSERT_EQ(neo_list_get_size(st->cases), 2);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_switch, def) {
  neo_location_t loc = create_location("switch (a){ default:}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_SWITCH);
  auto st = reinterpret_cast<neo_ast_statement_switch_t>(node);
  ASSERT_EQ(neo_list_get_size(st->cases), 1);
  auto item = neo_list_get_first(st->cases);
  auto cas = reinterpret_cast<neo_ast_switch_case_t>(neo_list_node_get(item));
  ASSERT_EQ(cas->condition, nullptr);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_switch, comment) {
  neo_location_t loc = create_location(
      "switch /*\ntest\n*/ ( /*\ntest\n*/ a /*\ntest\n*/ ) /*\ntest\n*/ { "
      "/*\ntest\n*/  case  /*\ntest\n*/ 1 /*\ntest\n*/ :  /*\ntest\n*/ default "
      "/*\ntest\n*/ : /*\ntest\n*/ }");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_SWITCH);
  auto st = reinterpret_cast<neo_ast_statement_switch_t>(node);
  ASSERT_EQ(neo_list_get_size(st->cases), 2);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_switch, no_condition) {
  neo_location_t loc = create_location("switch (){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_switch, no_body) {
  neo_location_t loc = create_location("switch (a)");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}