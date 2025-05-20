#include "compiler/statement_throw.h"
#include "compiler/expression.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_statement_throw_dispose(neo_allocator_t allocator,
                                            neo_ast_statement_throw_t node) {
  neo_allocator_free(allocator, node->value);
}
static neo_variable_t
neo_serialize_ast_statement_throw(neo_allocator_t allocator,
                                  neo_ast_statement_throw_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_STATEMENT_THROW"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  return variable;
}
static neo_ast_statement_throw_t
neo_ast_create_statement_throw(neo_allocator_t allocator) {
  neo_ast_statement_throw_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_throw);
  node->node.type = NEO_NODE_TYPE_STATEMENT_THROW;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_statement_throw;
  node->value = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_throw(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_statement_throw_t node = neo_ast_create_statement_throw(allocator);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "throw")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  neo_position_t cur = current;
  uint32_t line = current.line;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (cur.line == line) {
    node->value = TRY(neo_ast_read_expression(allocator, file, &cur)) {
      goto onerror;
    }
    if (node->value) {
      current = cur;
      uint32_t line = current.line;
      SKIP_ALL(allocator, file, &current, onerror);
      if (current.line == line) {
        if (*current.offset && *current.offset != ';' &&
            *current.offset != '}') {
          THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
                file, current.line, current.column);
          goto onerror;
        }
      }
    }
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return NULL;
}