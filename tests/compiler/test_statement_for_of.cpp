#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/ast_statement.h"
#include "neo.js/compiler/ast_statement_for_in.h"
#include "neo.js/compiler/scope.h"
#include "neo.js/core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_for_of : public neo_test {};
TEST_F(neo_test_statement_for_of, normal) {
  neo_location_t loc = create_location("for (const item of list){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_FOR_OF);
  auto st = reinterpret_cast<neo_ast_statement_for_in_t>(node);
  ASSERT_NE(st->left, nullptr);
  ASSERT_NE(st->right, nullptr);
  ASSERT_NE(st->body, nullptr);
}

TEST_F(neo_test_statement_for_of, comment) {
  neo_location_t loc = create_location(
      "for /*\ntest\n*/ ( /*\ntest\n*/ const /*\ntest\n*/ item /*\ntest\n*/ of "
      "/*\ntest\n*/  list /*\ntest\n*/ ) /*\ntest\n*/ {}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_FOR_OF);
  auto st = reinterpret_cast<neo_ast_statement_for_in_t>(node);
  ASSERT_NE(st->left, nullptr);
  ASSERT_NE(st->right, nullptr);
  ASSERT_NE(st->body, nullptr);
}