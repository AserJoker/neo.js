#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/ast_statement.h"
#include "neo.js/compiler/ast_statement_return.h"
#include "neo.js/compiler/scope.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_return : public neo_test {};
neo_location_t create_location(const char *src);
TEST_F(neo_test_statement_return, normal) {
  neo_location_t loc = create_location("return");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_RETURN);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_return, line_term) {
  neo_location_t loc = create_location("return\n123");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_RETURN);
  neo_ast_statement_return_t ret =
      reinterpret_cast<neo_ast_statement_return_t>(node);
  ASSERT_EQ(ret->value, nullptr);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_return, line_multicomment) {
  neo_location_t loc = create_location("return /*aaaa\n*/123");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_RETURN);
  neo_ast_statement_return_t ret =
      reinterpret_cast<neo_ast_statement_return_t>(node);
  ASSERT_EQ(ret->value, nullptr);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_return, value) {
  neo_location_t loc = create_location("return /*aaaa*/ 123");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_RETURN);
  neo_ast_statement_return_t ret =
      reinterpret_cast<neo_ast_statement_return_t>(node);
  ASSERT_NE(ret->value, nullptr);
  ASSERT_EQ(ret->value->type, NEO_NODE_TYPE_LITERAL_NUMERIC);
  neo_allocator_free(allocator, node);
}