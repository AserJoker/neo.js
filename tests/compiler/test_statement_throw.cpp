#include "compiler/ast_node.h"
#include "compiler/ast_statement.h"
#include "compiler/ast_statement_throw.h"
#include "core/allocator.h"
#include "core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_throw : public neo_test {};
neo_location_t create_location(const char *src);
TEST_F(neo_test_statement_throw, empty) {
  neo_location_t loc = create_location("throw");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_THROW);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_throw, newline) {
  neo_location_t loc = create_location("throw\n123");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_THROW);
  neo_ast_statement_throw_t ret =
      reinterpret_cast<neo_ast_statement_throw_t>(node);
  ASSERT_EQ(ret->value, nullptr);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_throw, multiline_comment) {
  neo_location_t loc = create_location("throw /*aaaa\n*/123");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_THROW);
  neo_ast_statement_throw_t ret =
      reinterpret_cast<neo_ast_statement_throw_t>(node);
  ASSERT_EQ(ret->value, nullptr);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_throw, comment) {
  neo_location_t loc = create_location("throw /*aaaa*/ 123");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_THROW);
  neo_ast_statement_throw_t ret =
      reinterpret_cast<neo_ast_statement_throw_t>(node);
  ASSERT_NE(ret->value, nullptr);
  ASSERT_EQ(ret->value->type, NEO_NODE_TYPE_LITERAL_NUMERIC);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_throw, value) {
  neo_location_t loc = create_location("throw 123");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_THROW);
  neo_ast_statement_throw_t ret =
      reinterpret_cast<neo_ast_statement_throw_t>(node);
  ASSERT_NE(ret->value, nullptr);
  ASSERT_EQ(ret->value->type, NEO_NODE_TYPE_LITERAL_NUMERIC);
  neo_allocator_free(allocator, node);
}