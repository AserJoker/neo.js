#include "compiler/ast_literal_null.h"
#include "compiler/ast_node.h"
#include "compiler/scope.h"
#include "core/allocator.h"
#include "core/location.h"
#include <gtest/gtest.h>
class neo_test_null : public testing::Test {
protected:
  neo_allocator_t allocator = NULL;

public:
  void SetUp() override { allocator = neo_create_allocator(NULL); }
  void TearDown() override {
    neo_delete_allocator(allocator);
    allocator = NULL;
  }
};
neo_location_t create_location(const char *src);
TEST_F(neo_test_null, normal) {
  neo_location_t loc = create_location("null");
  neo_ast_node_t node = neo_ast_read_literal_null(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_LITERAL_NULL);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_null, not_match) {
  neo_location_t loc = create_location("");
  neo_ast_node_t node = neo_ast_read_literal_null(allocator, "", &loc.end);
  ASSERT_EQ(node, nullptr);
}

TEST_F(neo_test_null, error) {
  neo_location_t loc = create_location("abc#");
  neo_ast_node_t node = neo_ast_read_literal_null(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
  neo_allocator_free(allocator, node);
}