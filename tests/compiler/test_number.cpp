#include "compiler/ast_literal_numeric.h"
#include "compiler/ast_node.h"
#include "compiler/scope.h"
#include "core/allocator.h"
#include "core/location.h"
#include <gtest/gtest.h>
class neo_test_number : public testing::Test {
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
TEST_F(neo_test_number, normal) {
  neo_location_t loc = create_location("1234");
  neo_ast_node_t node = neo_ast_read_literal_numeric(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_LITERAL_NUMERIC);
  char *str = neo_location_get(allocator, loc);
  ASSERT_EQ(std::string(str), "1234");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_number, bigint) {
  neo_location_t loc = create_location("1234n");
  neo_ast_node_t node = neo_ast_read_literal_numeric(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_LITERAL_BIGINT);
  char *str = neo_location_get(allocator, loc);
  ASSERT_EQ(std::string(str), "1234n");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, node);
}
TEST_F(neo_test_number, not_match) {
  neo_location_t loc = create_location("abc");
  neo_ast_node_t node = neo_ast_read_literal_numeric(allocator, "", &loc.end);
  ASSERT_EQ(node, nullptr);
}
TEST_F(neo_test_number, error) {
  neo_location_t loc = create_location("123abc");
  neo_ast_node_t node = neo_ast_read_literal_numeric(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_ERROR);
}