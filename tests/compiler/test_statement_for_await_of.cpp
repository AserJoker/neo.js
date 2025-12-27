#include "compiler/ast_node.h"
#include "compiler/ast_statement.h"
#include "compiler/ast_statement_for_in.h"
#include "compiler/scope.h"
#include "core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_for_await_of : public neo_test {};
TEST_F(neo_test_statement_for_await_of, normal) {
  neo_location_t loc = create_location("for await (const item of list){}");
  neo_compile_scope_t scope = neo_compile_scope_get_current();
  scope->is_async = true;
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  scope->is_async = false;
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_FOR_AWAIT_OF);
  auto st = reinterpret_cast<neo_ast_statement_for_in_t>(node);
  ASSERT_NE(st->left, nullptr);
  ASSERT_NE(st->right, nullptr);
  ASSERT_NE(st->body, nullptr);
}

TEST_F(neo_test_statement_for_await_of, comment) {
  neo_location_t loc = create_location(
      "for /*\ntest\n*/ await /*\ntest\n*/ ( /*\ntest\n*/ const /*\ntest\n*/ "
      "item /*\ntest\n*/ of /*\ntest\n*/  list /*\ntest\n*/ ) /*\ntest\n*/ {}");
  neo_compile_scope_t scope = neo_compile_scope_get_current();
  scope->is_async = true;
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  scope->is_async = false;
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_FOR_AWAIT_OF);
  auto st = reinterpret_cast<neo_ast_statement_for_in_t>(node);
  ASSERT_NE(st->left, nullptr);
  ASSERT_NE(st->right, nullptr);
  ASSERT_NE(st->body, nullptr);
}