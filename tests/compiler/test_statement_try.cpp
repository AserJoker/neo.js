#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/ast_statement.h"
#include "neo.js/compiler/ast_statement_try.h"
#include "neo.js/compiler/scope.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/location.h"
#include "test.hpp"
class neo_test_statement_try : public neo_test {};
TEST_F(neo_test_statement_try, catch_only) {
  neo_location_t loc = create_location("try{}catch(e){}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_TRY);
  auto st = reinterpret_cast<neo_ast_statement_try_t>(node);
  ASSERT_NE(st->catch_, nullptr);
  ASSERT_EQ(st->finally, nullptr);
  ASSERT_NE(st->body, nullptr);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_try, finally_only) {
  neo_location_t loc = create_location("try{}finally{}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_TRY);
  auto st = reinterpret_cast<neo_ast_statement_try_t>(node);
  ASSERT_EQ(st->catch_, nullptr);
  ASSERT_NE(st->finally, nullptr);
  ASSERT_NE(st->body, nullptr);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_statement_try, catch_finally) {
  neo_location_t loc = create_location("try{}catch(e){}finally{}");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_TRY);
  auto st = reinterpret_cast<neo_ast_statement_try_t>(node);
  ASSERT_NE(st->catch_, nullptr);
  ASSERT_NE(st->finally, nullptr);
  ASSERT_NE(st->body, nullptr);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_statement_try, comment) {
  neo_location_t loc =
      create_location("try /*\ntest\n*/ { /*\ntest\n*/ } /*\ntest\n*/ catch "
                      "/*\ntest\n*/ ( /*\ntest\n*/ e /*\ntest\n*/ ) "
                      "/*\ntest\n*/ { /*\ntest\n*/ } /*\ntest\n*/ finally "
                      "/*\ntest\n*/ { /*\ntest\n*/ }");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_TRY);
  auto st = reinterpret_cast<neo_ast_statement_try_t>(node);
  ASSERT_NE(st->catch_, nullptr);
  ASSERT_NE(st->finally, nullptr);
  ASSERT_NE(st->body, nullptr);
  neo_allocator_free(allocator, node);
}