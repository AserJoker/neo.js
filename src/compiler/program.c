
#include "compiler/program.h"
#include "compiler/interpreter.h"
#include "compiler/literal_string.h"
#include "compiler/statement.h"
#include "core/error.h"
#include <stdio.h>

static void neo_program_node_dispose(neo_allocator_t allocator,
                                     neo_ast_program_node_t program) {
  if (program->interpreter) {
    neo_allocator_free(allocator, program->interpreter);
  }
  neo_allocator_free(allocator, program->directives);
  neo_allocator_free(allocator, program->body);
}

static neo_ast_program_node_t
neo_create_program_node(neo_allocator_t allocator) {
  neo_ast_program_node_t node = (neo_ast_program_node_t)neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_program_node_t),
      neo_program_node_dispose);
  node->node.type = NEO_NODE_TYPE_PROGRAM;
  node->interpreter = NULL;
  neo_list_initialize_t initialize = {};
  initialize.auto_free = true;
  node->directives = neo_create_list(allocator, &initialize);
  node->body = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_program(neo_allocator_t allocator, const char *file,
                                    neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_program_node_t node = neo_create_program_node(allocator);
  node->interpreter = TRY(neo_ast_read_interpreter(allocator, file, &current)) {
    goto onerror;
  }
  SKIP_ALL();
  for (;;) {
    neo_ast_node_t directive =
        TRY(neo_ast_read_literal_string(allocator, file, &current)) {
      goto onerror;
    };
    if (!directive) {
      break;
    }
    neo_list_push(node->directives, directive);
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
    neo_ast_node_t statement =
        TRY(neo_ast_read_statement(allocator, file, &current)) {
      goto onerror;
    }
    if (!statement) {
      break;
    }
    neo_list_push(node->body, statement);
    SKIP_ALL();
  }
  node->node.location.end = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  if (node) {
    neo_allocator_free(allocator, node);
  }
  return NULL;
}