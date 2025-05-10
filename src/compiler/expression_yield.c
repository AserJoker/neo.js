#include "compiler/expression_yield.h"
#include "compiler/expression.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include <stdbool.h>

static void neo_ast_expression_yield_dispose(neo_allocator_t allocator,
                                             neo_ast_expression_yield_t self) {
  neo_allocator_free(allocator, self->value);
}

static neo_ast_expression_yield_t
neo_create_ast_expression_yield(neo_allocator_t allocator) {
  neo_ast_expression_yield_t node =
      (neo_ast_expression_yield_t)neo_allocator_alloc2(
          allocator, neo_ast_expression_yield);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_YIELD;
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