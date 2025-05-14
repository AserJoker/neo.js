#include "compiler/statement_return.h"
#include "compiler/node.h"
#include "compiler/statement_expression.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>

static void neo_ast_statement_return_dispose(neo_allocator_t allocator,
                                             neo_ast_statement_return_t node) {
  neo_allocator_free(allocator, node->value);
}

static neo_ast_statement_return_t
neo_ast_create_statement_return(neo_allocator_t allocator) {
  neo_ast_statement_return_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_return);
  node->node.type = NEO_NODE_TYPE_STATEMENT_RETURN;
  node->value = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_return(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_statement_return_t node = neo_ast_create_statement_return(allocator);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "return")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  neo_position_t cur = current;
  uint32_t line = current.line;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (cur.line == line) {
    node->value =
        TRY(neo_ast_read_statement_expression(allocator, file, &cur)) {
      goto onerror;
    }
    if (node->value) {
      current = cur;
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