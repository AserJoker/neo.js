#include "compiler/parser.h"
#include "compiler/node.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static char *neo_location_to_string(neo_allocator_t allocator,
                                    neo_location_t loc) {
  size_t len = loc.end.offset - loc.begin.offset;
  char *buf = neo_allocator_alloc(allocator, len + 1, NULL);
  strcpy(buf, loc.begin.offset);
  buf[len] = 0;
  return buf;
}

neo_ast_node_t neo_ast_parse_code(neo_allocator_t allocator, const char *file,
                                  const char *source) {
  neo_position_t current = {};
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
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  return program;
onerror:
  return NULL;
}