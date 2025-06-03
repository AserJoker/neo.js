#include "compiler/ast/expression_spread.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void
neo_ast_expression_spread_dispose(neo_allocator_t allocator,
                                  neo_ast_expression_spread_t node) {
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_expression_spread_resolve_closure(neo_allocator_t allocator,
                                          neo_ast_expression_spread_t self,
                                          neo_list_t closure) {
  self->value->resolve_closure(allocator, self->value, closure);
}

static neo_variable_t
neo_serialize_ast_expression_function(neo_allocator_t allocator,
                                      neo_ast_expression_spread_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_EXPRESSION_SPREAD"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  return variable;
}

static neo_ast_expression_spread_t
neo_create_ast_expression_spread(neo_allocator_t allocator) {
  neo_ast_expression_spread_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_spread);
  node->value = NULL;
  node->node.type = NEO_NODE_TYPE_EXPRESSION_SPREAD;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_expression_function;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_spread_resolve_closure;
  return node;
}

neo_ast_node_t neo_ast_read_expression_spread(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_spread_t node = NULL;
  neo_token_t token = NULL;
  token = TRY(neo_read_symbol_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token || !neo_location_is(token->location, "...")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_expression_spread(allocator);
  node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->value) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  node->node.location.file = file;
  node->node.location.begin = *position;
  node->node.location.end = current;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}