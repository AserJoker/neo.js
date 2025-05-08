#include "compiler/parser.h"
#include "compiler/literal_string.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include <gtest/gtest.h>
class TEST_parser : public testing::Test {
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
TEST_F(TEST_parser, read_program_empty) {
  const char *str = "";
  neo_ast_node_t node = neo_ast_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  neo_ast_program_t program = (neo_ast_program_t)node;
  ASSERT_EQ(program->interpreter, nullptr);
  ASSERT_EQ(neo_list_get_size(program->directives), 0);
  neo_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_program_comment) {
  const char *str = "/**\ncopyright xxxx*/\n//test comment\n\n\n";
  neo_ast_node_t node = neo_ast_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  neo_ast_program_t program = (neo_ast_program_t)node;
  ASSERT_EQ(program->interpreter, nullptr);
  ASSERT_EQ(neo_list_get_size(program->directives), 0);
  neo_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_interpreter) {
  const char *str = "#!/usr/bin/bash\n";
  neo_ast_node_t node = neo_ast_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  neo_ast_program_t program = (neo_ast_program_t)node;
  ASSERT_TRUE(program->interpreter != NULL);
  ASSERT_EQ(std::string(program->interpreter->location.begin.offset,
                        program->interpreter->location.end.offset),
            "#!/usr/bin/bash");
  neo_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_directive) {
  const char *str = "#!/usr/bin/bash\n'use strict'";
  neo_ast_node_t node = neo_ast_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  neo_ast_program_t program = (neo_ast_program_t)node;
  ASSERT_TRUE(neo_list_get_size(program->directives) != 0);
  neo_list_node_t it = neo_list_get_first(program->directives);
  neo_ast_literal_string_t directive =
      (neo_ast_literal_string_t)neo_list_node_get(it);
  ASSERT_EQ(std::string(directive->node.location.begin.offset,
                        directive->node.location.end.offset),
            "'use strict'");
  neo_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_directive2) {
  const char *str = "'use strict';";
  neo_ast_node_t node = neo_ast_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  neo_ast_program_t program = (neo_ast_program_t)node;
  ASSERT_TRUE(neo_list_get_size(program->directives) != 0);
  neo_list_node_t it = neo_list_get_first(program->directives);
  neo_ast_literal_string_t directive =
      (neo_ast_literal_string_t)neo_list_node_get(it);
  ASSERT_EQ(std::string(directive->node.location.begin.offset,
                        directive->node.location.end.offset),
            "'use strict'");
  neo_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_directive3) {
  const char *str = "'use a';\n'use b'";
  neo_ast_node_t node = neo_ast_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  neo_ast_program_t program = (neo_ast_program_t)node;
  ASSERT_EQ(neo_list_get_size(program->directives), 2);
  neo_list_node_t it = neo_list_get_first(program->directives);
  neo_ast_literal_string_t directive =
      (neo_ast_literal_string_t)neo_list_node_get(it);
  ASSERT_EQ(std::string(directive->node.location.begin.offset,
                        directive->node.location.end.offset),
            "'use a'");
  it = neo_list_node_next(it);
  directive = (neo_ast_literal_string_t)neo_list_node_get(it);
  ASSERT_EQ(std::string(directive->node.location.begin.offset,
                        directive->node.location.end.offset),
            "'use b'");
  neo_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_empty_statement) {
  const char *str = ";;/**test\n*/;\n;";
  neo_ast_node_t node = neo_ast_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(neo_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NEO_NODE_TYPE_PROGRAM);
  neo_ast_program_t program = (neo_ast_program_t)node;
  ASSERT_EQ(neo_list_get_size(program->body), 4);
  neo_allocator_free(_allocator, node);
}