#include "compiler/statement_expression.h"
#include "compiler/expression.h"
#include "compiler/node.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void
neo_ast_statement_expression_dispose(neo_allocator_t allocator,
                                     neo_ast_statement_expression_t node) {
  neo_allocator_free(allocator, node->expression);
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t
neo_serialize_ast_statement_expreesion(neo_allocator_t allocator,
                                       neo_ast_statement_expression_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_STATEMENT_EXPRESSION"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "expression",
                   neo_ast_node_serialize(allocator, node->expression));
  return variable;
}
static neo_ast_statement_expression_t
neo_create_ast_statement_expreesion(neo_allocator_t allocator) {
  neo_ast_statement_expression_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_expression);
  node->node.type = NEO_NODE_TYPE_STATEMENT_EXPRESSION;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn)neo_serialize_ast_statement_expreesion;
  node->expression = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_expression(neo_allocator_t allocator,
                                                 const char *file,
                                                 neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_expression_t node = NULL;
  neo_ast_node_t expression =
      TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  }
  if (!expression) {
    return NULL;
  }
  node = neo_create_ast_statement_expreesion(allocator);
  node->expression = expression;
  uint32_t line = current.line;
  SKIP_ALL(allocator, file, &current, onerror);
  if (current.line == line) {
    if (*current.offset && *current.offset != ';' && *current.offset != '}') {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
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