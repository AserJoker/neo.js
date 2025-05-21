#include "compiler/statement_for_of.h"
#include "compiler/expression.h"
#include "compiler/identifier.h"
#include "compiler/pattern_array.h"
#include "compiler/pattern_object.h"
#include "compiler/scope.h"
#include "compiler/statement.h"
#include "compiler/token.h"
#include "core/variable.h"
#include <stdio.h>
static void neo_ast_statement_for_of_dispose(neo_allocator_t allocator,
                                             neo_ast_statement_for_of_t node) {
  neo_allocator_free(allocator, node->left);
  neo_allocator_free(allocator, node->right);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t
neo_serialize_ast_statement_for_of(neo_allocator_t allocator,
                                   neo_ast_statement_for_of_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_STATEMENT_FOR_OF"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "left",
                   neo_ast_node_serialize(allocator, node->left));
  neo_variable_set(variable, "right",
                   neo_ast_node_serialize(allocator, node->right));
  neo_variable_set(variable, "body",
                   neo_ast_node_serialize(allocator, node->body));
  switch (node->kind) {
  case NEO_AST_DECLARATION_VAR:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_VAR"));
    break;
  case NEO_AST_DECLARATION_CONST:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_CONST"));
    break;
  case NEO_AST_DECLARATION_LET:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_LET"));
    break;
  case NEO_AST_DECLARATION_NONE:
    neo_variable_set(
        variable, "kind",
        neo_create_variable_string(allocator, "NEO_AST_DECLARATION_NONE"));
    break;
  }
  return variable;
}
static neo_ast_statement_for_of_t
neo_create_ast_statement_for_of(neo_allocator_t allocator) {
  neo_ast_statement_for_of_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_for_of);
  node->node.type = NEO_NODE_TYPE_STATEMENT_FOR_OF;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_statement_for_of;
  node->left = NULL;
  node->right = NULL;
  node->body = NULL;
  node->kind = NEO_AST_DECLARATION_NONE;
  return node;
}
neo_ast_node_t neo_ast_read_statement_for_of(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_for_of_t node = neo_create_ast_statement_for_of(allocator);
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
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
  scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_BLOCK);
  SKIP_ALL(allocator, file, &current, onerror);
  neo_position_t cur = current;
  token = neo_read_identify_token(allocator, file, &cur);
  if (token && neo_location_is(token->location, "const")) {
    node->kind = NEO_AST_DECLARATION_CONST;
    current = cur;
    SKIP_ALL(allocator, file, &current, onerror);
  } else if (token && neo_location_is(token->location, "let")) {
    node->kind = NEO_AST_DECLARATION_LET;
    current = cur;
    SKIP_ALL(allocator, file, &current, onerror);
  } else if (token && neo_location_is(token->location, "var")) {
    node->kind = NEO_AST_DECLARATION_VAR;
    current = cur;
    SKIP_ALL(allocator, file, &current, onerror);
  } else {
    node->kind = NEO_AST_DECLARATION_NONE;
  }
  neo_allocator_free(allocator, token);
  node->left = TRY(neo_ast_read_pattern_object(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->left) {
    node->left = TRY(neo_ast_read_pattern_array(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->left) {
    node->left = TRY(neo_ast_read_identifier(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->left) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "of")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->right = TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->right) {
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
