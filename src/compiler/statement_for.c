#include "compiler/statement_for.h"
#include "compiler/declaration_variable.h"
#include "compiler/expression.h"
#include "compiler/statement.h"
#include "compiler/token.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_statement_for_dispose(neo_allocator_t allocator,
                                          neo_ast_statement_for_t node) {
  neo_allocator_free(allocator, node->initialize);
  neo_allocator_free(allocator, node->condition);
  neo_allocator_free(allocator, node->after);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t
neo_serialize_ast_statement_for(neo_allocator_t allocator,
                                neo_ast_statement_for_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_STATEMENT_FOR"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "initialize",
                   neo_ast_node_serialize(allocator, node->initialize));
  neo_variable_set(variable, "condition",
                   neo_ast_node_serialize(allocator, node->condition));
  neo_variable_set(variable, "body",
                   neo_ast_node_serialize(allocator, node->body));
  return variable;
}
static neo_ast_statement_for_t
neo_create_ast_statement_for(neo_allocator_t allocator) {
  neo_ast_statement_for_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_for);
  node->node.type = NEO_NODE_TYPE_STATEMENT_FOR;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_statement_for;
  node->initialize = NULL;
  node->condition = NULL;
  node->after = NULL;
  node->body = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_statement_for(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_for_t node = neo_create_ast_statement_for(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "for")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '(') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  node->initialize = TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->initialize) {
    node->initialize =
        TRY(neo_ast_read_declaration_variable(allocator, file, &current)) {
      goto onerror;
    }
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ';') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->condition = TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ';') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->after = TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ')') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->body = TRY(neo_ast_read_statement(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->body) {
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