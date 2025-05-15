#include "compiler/statement_labeled.h"
#include "compiler/identifier.h"
#include "compiler/node.h"
#include "compiler/statement.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>
static void
neo_ast_statement_labeled_dispose(neo_allocator_t allocator,
                                  neo_ast_statement_labeled_t node) {
  neo_allocator_free(allocator, node->label);
  neo_allocator_free(allocator, node->statement);
}

static neo_ast_statement_labeled_t
neo_create_ast_statement_labeled(neo_allocator_t allocator) {
  neo_ast_statement_labeled_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_labeled);
  node->node.type = NEO_NODE_TYPE_STATEMENT_LABELED;
  node->label = NULL;
  node->statement = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_labeled(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_statement_labeled_t node =
      neo_create_ast_statement_labeled(allocator);
  node->label = neo_ast_read_identifier(allocator, file, &current);
  if (!node->label) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_symbol_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, ":")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->statement = TRY(neo_ast_read_statement(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->statement) {
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
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}
