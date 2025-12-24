#include "compiler/ast_node.h"
#include "compiler/ast_statement.h"
#include "compiler/ast_statement_continue.h"
#include "core/allocator.h"
#include "core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_continue : public neo_test {};
TEST_F(neo_test_statement_continue, normal) {
  neo_location_t loc = create_location("continue");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_CONTINUE);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_continue, label) {
  neo_location_t loc = create_location("continue test");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_CONTINUE);
  neo_ast_statement_continue_t statement =
      reinterpret_cast<neo_ast_statement_continue_t>(node);
  ASSERT_NE(statement->label, nullptr);
  ASSERT_EQ(statement->label->type, NEO_NODE_TYPE_IDENTIFIER);
  char *s = neo_location_get_raw(allocator, statement->label->location);
  ASSERT_EQ(std::string(s), "test");
  neo_allocator_free(allocator, s);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_continue, new_line) {
  neo_location_t loc = create_location("continue\ntest");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_CONTINUE);
  neo_allocator_free(allocator, node);
}
