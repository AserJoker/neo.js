#include "compiler/statement_debugger.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"

static neo_ast_statement_debugger_t
neo_create_statement_debugger(neo_allocator_t allocator) {
  neo_ast_statement_debugger_t node =
      (neo_ast_statement_debugger_t)neo_allocator_alloc(
          allocator, sizeof(struct _neo_ast_statement_debugger_t), NULL);
  node->node.type = NEO_NODE_TYPE_STATEMENT_DEBUGGER;
  return node;
}

neo_ast_node_t neo_ast_read_statement_debugger(neo_allocator_t allocator,
                                               const char *file,
                                               neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_debugger_t node = NULL;
  neo_token_t token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  }
  if (!neo_location_is(token->location, "debugger")) {
    neo_allocator_free(allocator, token);
    return NULL;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_statement_debugger(allocator);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  return &node->node;
onerror:
  if (node) {
    neo_allocator_free(allocator, node);
  }
  return NULL;
}