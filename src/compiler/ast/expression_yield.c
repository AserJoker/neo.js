#include "compiler/ast/expression_yield.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdbool.h>

static void neo_ast_expression_yield_dispose(neo_allocator_t allocator,
                                             neo_ast_expression_yield_t node) {
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_expression_yield(neo_allocator_t allocator,
                                   neo_ast_expression_yield_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_EXPRESSION_YIELD"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  neo_variable_set(variable, "degelate",
                   neo_create_variable_boolean(allocator, node->degelate));
  return variable;
}

static neo_ast_expression_yield_t
neo_create_ast_expression_yield(neo_allocator_t allocator) {
  neo_ast_expression_yield_t node =
      (neo_ast_expression_yield_t)neo_allocator_alloc2(
          allocator, neo_ast_expression_yield);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_YIELD;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_expression_yield;
  node->value = NULL;
  node->degelate = false;
  return node;
}

neo_ast_node_t neo_ast_read_expression_yield(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_expression_yield_t node = NULL;
  token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token) {
    return NULL;
  }
  if (!neo_location_is(token->location, "yield")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node = neo_create_ast_expression_yield(allocator);
  if (*current.offset == '*') {
    current.offset++;
    current.column++;
    node->degelate = true;
  }
  node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}