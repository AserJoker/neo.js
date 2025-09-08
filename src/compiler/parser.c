#include "compiler/parser.h"
#include "compiler/ast/node.h"
#include "compiler/ast/program.h"
#include "compiler/program.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

neo_ast_node_t neo_ast_parse_code(neo_allocator_t allocator, const char *file,
                                  const char *source) {
  neo_position_t current = {0};
  current.column = 1;
  current.line = 1;
  current.offset = source;
  SKIP_ALL(allocator, file, &current, onerror);
  neo_ast_node_t program =
      TRY(neo_ast_read_program(allocator, file, &current)) {
    goto onerror;
  };
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '\0') {
    neo_allocator_free(allocator, program);
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  return program;
onerror:
  return NULL;
}

neo_program_t neo_ast_write_node(neo_allocator_t allocator, const char *file,
                                 neo_ast_node_t node) {
  neo_program_t program = neo_create_program(allocator, file);
  neo_write_context_t ctx = neo_create_write_context(allocator, program);
  TRY(node->write(allocator, ctx, node)) {
    neo_allocator_free(allocator, ctx);
    neo_allocator_free(allocator, program);
    return NULL;
  }
  neo_allocator_free(allocator, ctx);
  return program;
}