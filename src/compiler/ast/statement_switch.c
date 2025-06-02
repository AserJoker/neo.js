#include "compiler/ast/statement_switch.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/ast/switch_case.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_statement_switch_dispose(neo_allocator_t allocator,
                                             neo_ast_statement_switch_t node) {
  neo_allocator_free(allocator, node->cases);
  neo_allocator_free(allocator, node->condition);
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t
neo_serialize_ast_statement_switch(neo_allocator_t allocator,
                                   neo_ast_statement_switch_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_STATEMENT_SWITCH"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "condition",
                   neo_ast_node_serialize(allocator, node->condition));
  neo_variable_set(variable, "cases",
                   neo_ast_node_list_serialize(allocator, node->cases));
  return variable;
}
static neo_ast_statement_switch_t
neo_create_ast_statement_switch(neo_allocator_t allocator) {
  neo_ast_statement_switch_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_switch);
  node->node.type = NEO_NODE_TYPE_STATEMENT_SWITCH;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_statement_switch;
  node->condition = NULL;
  neo_list_initialize_t initialize = {true};
  node->cases = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_statement_switch(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_switch_t node = neo_create_ast_statement_switch(allocator);
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "switch")) {
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
  SKIP_ALL(allocator, file, &current, onerror);
  node->condition = TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->condition) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
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
  if (*current.offset != '{') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_BLOCK);
  SKIP_ALL(allocator, file, &current, onerror);
  for (;;) {
    neo_ast_node_t cas =
        TRY(neo_ast_read_switch_case(allocator, file, &current)) {
      goto onerror;
    }
    if (!cas) {
      break;
    }
    neo_list_push(node->cases, cas);
    SKIP_ALL(allocator, file, &current, onerror);
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '}') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  node->node.scope = neo_compile_scope_pop(scope);
  *position = current;
  return &node->node;
onerror:
  if (scope && !node->node.scope) {
    scope = neo_compile_scope_pop(scope);
    neo_allocator_free(allocator, scope);
  }
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}