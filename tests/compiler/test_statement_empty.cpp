#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/ast_statement.h"
#include "neo.js/compiler/scope.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_statement_empty : public neo_test {};
neo_location_t create_location(const char *src);
TEST_F(neo_test_statement_empty, normal) {
  neo_location_t loc = create_location(";");
  neo_ast_node_t node = neo_ast_read_statement(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_STATEMENT_EMPTY);
  neo_allocator_free(allocator, node);
}