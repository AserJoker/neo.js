
#include "compiler/program.h"
#include "compiler/interpreter.h"
#include "compiler/literal_string.h"
#include "compiler/statement_empty.h"
#include "core/error.h"
#include <stdio.h>

static noix_ast_node_t noix_read_statement(noix_allocator_t allocator,
                                           const char *file,
                                           noix_position_t *position) {
  noix_ast_node_t node = NULL;
  node = TRY(noix_read_empty_statement(allocator, file, position)) {
    goto onerror;
  }
  return node;
onerror:
  return NULL;
}

static void noix_program_node_dispose(noix_allocator_t allocator,
                                      noix_program_node_t program) {
  if (program->interpreter) {
    noix_allocator_free(allocator, program->interpreter);
  }
  noix_allocator_free(allocator, program->directives);
  noix_allocator_free(allocator, program->body);
}

static noix_program_node_t
noix_create_program_node(noix_allocator_t allocator) {
  noix_program_node_t node = (noix_program_node_t)noix_allocator_alloc(
      allocator, sizeof(struct _noix_program_node_t),
      noix_program_node_dispose);
  node->node.type = NOIX_NODE_TYPE_PROGRAM;
  node->interpreter = NULL;
  noix_list_initialize initialize = {};
  initialize.auto_free = true;
  node->directives = noix_create_list(allocator, &initialize);
  node->body = noix_create_list(allocator, &initialize);
  return node;
}

noix_ast_node_t noix_read_program(noix_allocator_t allocator, const char *file,
                                  noix_position_t *position) {
  noix_position_t current = *position;
  noix_program_node_t node = noix_create_program_node(allocator);
  node->interpreter = TRY(noix_read_interpreter(allocator, file, &current)) {
    goto onerror;
  }
  SKIP_ALL();
  for (;;) {
    noix_ast_node_t directive =
        TRY(noix_read_string_literal(allocator, file, &current)) {
      goto onerror;
    };
    if (!directive) {
      break;
    }
    noix_list_push(node->directives, directive);
    int32_t line = current.line;
    SKIP_ALL();
    if (current.line == line) {
      if (*current.offset == ';') {
        current.offset++;
        current.column++;
        SKIP_ALL();
      } else if (*current.offset == '\0') {
        break;
      } else {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
    }
  }
  SKIP_ALL();
  for (;;) {
    noix_ast_node_t statement =
        TRY(noix_read_statement(allocator, file, &current)) {
      goto onerror;
    }
    if (!statement) {
      break;
    }
    noix_list_push(node->body, statement);
    SKIP_ALL();
  }
  node->node.location.end = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  if (node) {
    noix_allocator_free(allocator, node);
  }
  return NULL;
}