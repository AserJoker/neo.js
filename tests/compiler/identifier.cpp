#include "compiler/node.h"
#include "compiler/parser.h"
#include "compiler/program.h"
#include "compiler/statement_expression.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include <gtest/gtest.h>
class TEST_identifier : public testing::Test {
protected:
  neo_allocator_t _allocator;

public:
  void SetUp() override {
    _allocator = neo_create_default_allocator();
    neo_error_initialize(_allocator);
  }

  void TearDown() override {
    if (neo_has_error()) {
      neo_error_t error = neo_poll_error(__FUNCTION__, __FILE__, __LINE__);
      if (error) {
        char *msg = neo_error_to_string(error);
        std::cerr << msg << std::endl;
        neo_allocator_free(_allocator, msg);
        neo_allocator_free(_allocator, error);
      }
    }
    neo_delete_allocator(_allocator);
    _allocator = NULL;
  }
};

TEST_F(TEST_identifier, identifier) {
  const char *str = "a";
  neo_ast_node_t node = neo_ast_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(neo_has_error());
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  neo_ast_program_t program = (neo_ast_program_t)node;
  ASSERT_EQ(neo_list_get_size(program->body), 1);
  neo_ast_statement_expression_t sts_expr =
      (neo_ast_statement_expression_t)(neo_list_node_get(
          neo_list_get_first(program->body)));
  ASSERT_NE(sts_expr, nullptr);
  ASSERT_NE(sts_expr->expression, nullptr);
  ASSERT_EQ(sts_expr->expression->type, NEO_NODE_TYPE_IDENTIFIER);
  ASSERT_TRUE(neo_location_is(sts_expr->expression->location, "a"));
  neo_allocator_free(_allocator, node);
}