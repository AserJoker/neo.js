
#include "compiler/ast/program.h"
#include "compiler/ast/directive.h"
#include "compiler/ast/interpreter.h"
#include "compiler/ast/node.h"
#include "compiler/ast/statement.h"
#include "compiler/scope.h"
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

static void neo_ast_program_resolve_closure(neo_allocator_t allocator,
                                            neo_ast_program_t self,
                                            neo_list_t closure) {
  for (neo_list_node_t it = neo_list_get_first(self->body);
       it != neo_list_get_tail(self->body); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
}

static neo_variable_t neo_serialize_ast_program(neo_allocator_t allocator,
                                                neo_ast_program_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_PROGRAM"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
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
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_program;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_program_resolve_closure;
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
  neo_compile_scope_t scope =
      neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_FUNCTION);
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
  node->node.scope = neo_compile_scope_pop(scope);
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  scope = neo_compile_scope_pop(scope);
  neo_allocator_free(allocator, scope);
  return NULL;
}