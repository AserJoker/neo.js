#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/ast_statement.h"
#include "neojs/compiler/ast_statement_break.h"
#include "neojs/core/allocator.h"
#include "neojs/core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_break : public neo_test {};
neo_location_t create_location(const char *src);
TEST_F(neo_test_statement_break, normal) {
  neo_location_t loc = create_location("break");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_BREAK);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_break, label) {
  neo_location_t loc = create_location("break test");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_BREAK);
  neo_ast_statement_break_t statement =
      reinterpret_cast<neo_ast_statement_break_t>(node);
  ASSERT_NE(statement->label, nullptr);
  ASSERT_EQ(statement->label->type, NEO_NODE_TYPE_IDENTIFIER);
  char *s = neo_location_get_raw(allocator, statement->label->location);
  ASSERT_EQ(std::string(s), "test");
  neo_allocator_free(allocator, s);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_break, newline) {
  neo_location_t loc = create_location("break \n test");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_BREAK);
  neo_ast_statement_break_t statement =
      reinterpret_cast<neo_ast_statement_break_t>(node);
  ASSERT_EQ(statement->label, nullptr);
  neo_allocator_free(allocator, node);
}