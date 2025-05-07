#include "compiler/parser.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include <stddef.h>
#include <stdio.h>

noix_ast_node_t noix_parse_code(noix_allocator_t allocator, const char *file,
                                const char *source) {
  noix_position_t current = {};
  current.column = 1;
  current.line = 1;
  current.offset = source;
  SKIP_ALL();
  noix_ast_node_t program =
      TRY(noix_ast_read_program(allocator, file, &current)) {
    goto onerror;
  };
  SKIP_ALL();
  if (*current.offset != '\0') {
    noix_allocator_free(allocator, program);
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  return program;
onerror:
  return NULL;
}