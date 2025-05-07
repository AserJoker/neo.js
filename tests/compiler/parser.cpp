#include "compiler/parser.h"
#include "compiler/literal_string.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include <gtest/gtest.h>
class TEST_parser : public testing::Test {
protected:
  noix_allocator_t _allocator;

public:
  void SetUp() override {
    _allocator = noix_create_default_allocator();
    noix_error_initialize(_allocator);
  }

  void TearDown() override {
    if (noix_has_error()) {
      noix_error_t error = noix_poll_error(__FUNCTION__, __FILE__, __LINE__);
      if (error) {
        char *msg = noix_error_to_string(error);
        std::cerr << msg << std::endl;
        noix_allocator_free(_allocator, msg);
        noix_allocator_free(_allocator, error);
      }
    }
    noix_delete_allocator(_allocator);
    _allocator = NULL;
  }
};
TEST_F(TEST_parser, read_program_empty) {
  const char *str = "";
  noix_ast_node_t node = noix_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NOIX_NODE_TYPE_PROGRAM);
  noix_ast_program_node_t program = (noix_ast_program_node_t)node;
  ASSERT_EQ(program->interpreter, nullptr);
  ASSERT_EQ(noix_list_get_size(program->directives), 0);
  noix_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_program_comment) {
  const char *str = "/**\ncopyright xxxx*/\n//test comment\n\n\n";
  noix_ast_node_t node = noix_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NOIX_NODE_TYPE_PROGRAM);
  noix_ast_program_node_t program = (noix_ast_program_node_t)node;
  ASSERT_EQ(program->interpreter, nullptr);
  ASSERT_EQ(noix_list_get_size(program->directives), 0);
  noix_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_interpreter) {
  const char *str = "#!/usr/bin/bash\n";
  noix_ast_node_t node = noix_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NOIX_NODE_TYPE_PROGRAM);
  noix_ast_program_node_t program = (noix_ast_program_node_t)node;
  ASSERT_TRUE(program->interpreter != NULL);
  ASSERT_EQ(std::string(program->interpreter->location.begin.offset,
                        program->interpreter->location.end.offset),
            "#!/usr/bin/bash");
  noix_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_directive) {
  const char *str = "#!/usr/bin/bash\n'use strict'";
  noix_ast_node_t node = noix_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NOIX_NODE_TYPE_PROGRAM);
  noix_ast_program_node_t program = (noix_ast_program_node_t)node;
  ASSERT_TRUE(noix_list_get_size(program->directives) != 0);
  noix_list_node_t it = noix_list_get_first(program->directives);
  noix_ast_literal_string_node_t directive =
      (noix_ast_literal_string_node_t)noix_list_node_get(it);
  ASSERT_EQ(std::string(directive->node.location.begin.offset,
                        directive->node.location.end.offset),
            "'use strict'");
  noix_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_directive2) {
  const char *str = "'use strict';";
  noix_ast_node_t node = noix_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NOIX_NODE_TYPE_PROGRAM);
  noix_ast_program_node_t program = (noix_ast_program_node_t)node;
  ASSERT_TRUE(noix_list_get_size(program->directives) != 0);
  noix_list_node_t it = noix_list_get_first(program->directives);
  noix_ast_literal_string_node_t directive =
      (noix_ast_literal_string_node_t)noix_list_node_get(it);
  ASSERT_EQ(std::string(directive->node.location.begin.offset,
                        directive->node.location.end.offset),
            "'use strict'");
  noix_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_directive3) {
  const char *str = "'use a';\n'use b'";
  noix_ast_node_t node = noix_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NOIX_NODE_TYPE_PROGRAM);
  noix_ast_program_node_t program = (noix_ast_program_node_t)node;
  ASSERT_TRUE(noix_list_get_size(program->directives) == 2);
  noix_list_node_t it = noix_list_get_first(program->directives);
  noix_ast_literal_string_node_t directive =
      (noix_ast_literal_string_node_t)noix_list_node_get(it);
  ASSERT_EQ(std::string(directive->node.location.begin.offset,
                        directive->node.location.end.offset),
            "'use a'");
  it = noix_list_node_next(it);
  directive = (noix_ast_literal_string_node_t)noix_list_node_get(it);
  ASSERT_EQ(std::string(directive->node.location.begin.offset,
                        directive->node.location.end.offset),
            "'use b'");
  noix_allocator_free(_allocator, node);
}

TEST_F(TEST_parser, read_empty_statement) {
  const char *str = ";;/**test\n*/;\n;";
  noix_ast_node_t node = noix_parse_code(_allocator, "test.js", str);
  ASSERT_FALSE(noix_has_error());
  ASSERT_TRUE(node != NULL);
  ASSERT_EQ(node->type, NOIX_NODE_TYPE_PROGRAM);
  noix_ast_program_node_t program = (noix_ast_program_node_t)node;
  ASSERT_EQ(noix_list_get_size(program->body), 4);
  noix_allocator_free(_allocator, node);
}