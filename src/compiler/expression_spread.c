#include "compiler/expression_spread.h"
#include "compiler/expression.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>

static void
neo_ast_expression_spread_dispose(neo_allocator_t allocator,
                                  neo_ast_expression_spread_t node) {
  neo_allocator_free(allocator, node->value);
}

neo_ast_expression_spread_t
neo_create_ast_expression_spread(neo_allocator_t allocator) {
  neo_ast_expression_spread_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_spread);
  node->value = NULL;
  node->node.type = NEO_NODE_TYPE_EXPRESSION_SPREAD;
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