#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/ast_program.h"
#include "neo.js/compiler/scope.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/list.h"
#include "neo.js/core/location.h"
#include "test.hpp"
#include <gtest/gtest.h>
class neo_test_program : public neo_test {};
neo_location_t create_location(const char *src);
TEST_F(neo_test_program, empty) {
  neo_location_t loc = {};
  neo_ast_node_t node = nullptr;
  neo_ast_program_t program = nullptr;
  loc = create_location("");
  node = neo_ast_read_program(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  program = reinterpret_cast<neo_ast_program_t>(node);
  ASSERT_EQ(program->interpreter, nullptr);
  ASSERT_EQ(neo_list_get_size(program->directives), 0);
  ASSERT_EQ(neo_list_get_size(program->body), 0);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_program, interpreter) {
  neo_location_t loc = {};
  neo_ast_node_t node = nullptr;
  neo_ast_program_t program = nullptr;
  loc = create_location("#!/usr/bin/neo\n");
  node = neo_ast_read_program(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  program = reinterpret_cast<neo_ast_program_t>(node);
  ASSERT_NE(program->interpreter, nullptr);
  ASSERT_EQ(program->interpreter->type, NEO_NODE_TYPE_INTERPRETER_DIRECTIVE);
  char *str = neo_location_get(allocator, program->interpreter->location);
  ASSERT_EQ(std::string(str), "#!/usr/bin/neo");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, node);
}

TEST_F(neo_test_program, directives) {
  neo_location_t loc = {};
  neo_ast_node_t node = nullptr;
  neo_ast_program_t program = nullptr;
  loc = create_location("#!/usr/bin/neo\n"
                        "/* test comment */"
                        "'use client'\n");
  node = neo_ast_read_program(allocator, "", &loc.end);
  ASSERT_NE(node, nullptr);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  program = reinterpret_cast<neo_ast_program_t>(node);
  ASSERT_EQ(neo_list_get_size(program->directives), 1);
  neo_list_node_t item = neo_list_get_first(program->directives);
  neo_ast_node_t directive =
      reinterpret_cast<neo_ast_node_t>(neo_list_node_get(item));
  ASSERT_EQ(directive->type, NEO_NODE_TYPE_DIRECTIVE);
  char *str = neo_location_get(allocator, directive->location);
  ASSERT_EQ(std::string(str), "'use client'");
  neo_allocator_free(allocator, str);
  neo_allocator_free(allocator, node);
}