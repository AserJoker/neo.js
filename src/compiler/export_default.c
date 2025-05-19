#include "compiler/export_default.h"
#include "compiler/expression.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>

static void neo_ast_export_default_dispose(neo_allocator_t allocator,
                                           neo_ast_export_default_t node) {
  neo_allocator_free(allocator, node->value);
}

static neo_ast_export_default_t
neo_create_ast_export_default(neo_allocator_t allocator) {
  neo_ast_export_default_t node =
      neo_allocator_alloc2(allocator, neo_ast_export_default);
  node->node.type = NEO_NODE_TYPE_EXPORT_DEFAULT;
  node->value = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_export_default(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_export_default_t node = neo_create_ast_export_default(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "default")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->value) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
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