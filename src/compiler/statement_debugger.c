#include "compiler/statement_debugger.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"

static void neo_statement_debugger(neo_allocator_t allocator,
                                   neo_ast_statement_debugger_t node) {
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_statement_debugger(neo_allocator_t allocator,
                                     neo_ast_statement_debugger_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_STATEMENT_DEBUGGER"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_statement_debugger_t
neo_create_statement_debugger(neo_allocator_t allocator) {
  neo_ast_statement_debugger_t node =
      (neo_ast_statement_debugger_t)neo_allocator_alloc(
          allocator, sizeof(struct _neo_ast_statement_debugger_t), NULL);
  node->node.type = NEO_NODE_TYPE_STATEMENT_DEBUGGER;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_statement_debugger;
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
  if (!token) {
    return NULL;
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
  neo_allocator_free(allocator, node);
  return NULL;
}