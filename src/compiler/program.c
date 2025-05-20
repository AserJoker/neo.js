
#include "compiler/program.h"
#include "compiler/directive.h"
#include "compiler/interpreter.h"
#include "compiler/node.h"
#include "compiler/statement.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_program_dispose(neo_allocator_t allocator,
                                    neo_ast_program_t node) {
  neo_allocator_free(allocator, node->interpreter);
  neo_allocator_free(allocator, node->directives);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t neo_serialize_ast_program(neo_allocator_t allocator,
                                                neo_ast_program_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_PROGRAM"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "interpreter",
                   neo_ast_node_serialize(allocator, node->interpreter));
  neo_variable_set(variable, "directives",
                   neo_ast_node_list_serialize(allocator, node->directives));
  neo_variable_set(variable, "body",
                   neo_ast_node_list_serialize(allocator, node->body));
  return variable;
}

static neo_ast_program_t neo_create_ast_program(neo_allocator_t allocator) {
  neo_ast_program_t node = neo_allocator_alloc2(allocator, neo_ast_program);
  node->node.type = NEO_NODE_TYPE_PROGRAM;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_program;
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
  neo_ast_program_t node = neo_create_ast_program(allocator);
  node->interpreter = TRY(neo_ast_read_interpreter(allocator, file, &current)) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  for (;;) {
    neo_ast_node_t directive =
        TRY(neo_ast_read_directive(allocator, file, &current)) {
      goto onerror;
    };
    if (!directive) {
      break;
    }
    neo_list_push(node->directives, directive);
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset == ';') {
      current.offset++;
      current.column++;
      SKIP_ALL(allocator, file, &current, onerror);
    }
  }
  SKIP_ALL(allocator, file, &current, onerror);
  for (;;) {
    neo_ast_node_t statement =
        TRY(neo_ast_read_statement(allocator, file, &current)) {
      goto onerror;
    }
    if (!statement) {
      break;
    }
    neo_list_push(node->body, statement);
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset == ';') {
      current.offset++;
      current.column++;
      SKIP_ALL(allocator, file, &current, onerror);
    }
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}