#include "compiler/parser.h"
#include "compiler/ast_node.h"
#include "compiler/ast_program.h"
#include "compiler/program.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/position.h"
#include <stddef.h>

neo_ast_node_t neo_ast_parse_code(neo_allocator_t allocator, const char *file,
                                  const char *source) {
  neo_position_t current = {0};
  neo_ast_node_t program = NULL;
  neo_ast_node_t error = NULL;
  current.column = 1;
  current.line = 1;
  current.offset = source;
  error = neo_skip_all(allocator, file, &current);
  if (error && error->type == NEO_NODE_TYPE_ERROR) {
    goto onerror;
  }
  program = neo_ast_read_program(allocator, file, &current);
  if (program->type == NEO_NODE_TYPE_ERROR) {
    goto onerror;
  };
  error = neo_skip_all(allocator, file, &current);
  if (error && error->type == NEO_NODE_TYPE_ERROR) {
    goto onerror;
  }
  if (*current.offset != '\0') {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
  }
  return program;
onerror:
  neo_allocator_free(allocator, program);
  return error;
}

neo_js_program_t neo_ast_write_node(neo_allocator_t allocator, const char *file,
                                    neo_ast_node_t node) {
  neo_js_program_t program = neo_create_js_program(allocator, file);
  neo_write_context_t ctx = neo_create_write_context(allocator, program);
  node->write(allocator, ctx, node);
  neo_allocator_free(allocator, ctx);
  return program;
}