#include "compiler/ast/statement_labeled.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/ast/statement.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>
static void
neo_ast_statement_labeled_dispose(neo_allocator_t allocator,
                                  neo_ast_statement_labeled_t node) {
  neo_allocator_free(allocator, node->label);
  neo_allocator_free(allocator, node->statement);
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t
neo_serialize_ast_statement_labeled(neo_allocator_t allocator,
                                    neo_ast_statement_labeled_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_STATEMENT_LABELED"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "label",
                   neo_ast_node_serialize(allocator, node->label));
  neo_variable_set(variable, "statement",
                   neo_ast_node_serialize(allocator, node->statement));
  return variable;
}
static neo_ast_statement_labeled_t
neo_create_ast_statement_labeled(neo_allocator_t allocator) {
  neo_ast_statement_labeled_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_labeled);
  node->node.type = NEO_NODE_TYPE_STATEMENT_LABELED;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_statement_labeled;
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
