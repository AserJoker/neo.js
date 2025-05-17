#include "compiler/statement_for_await_of.h"
#include "compiler/expression.h"
#include "compiler/identifier.h"
#include "compiler/pattern_array.h"
#include "compiler/pattern_object.h"
#include "compiler/statement.h"
#include "compiler/token.h"
#include <stdio.h>
static void
neo_ast_statement_for_await_of_dispose(neo_allocator_t allocator,
                                       neo_ast_statement_for_await_of_t node) {
  neo_allocator_free(allocator, node->left);
  neo_allocator_free(allocator, node->right);
  neo_allocator_free(allocator, node->body);
}

static neo_ast_statement_for_await_of_t
neo_create_ast_statement_for_await_of(neo_allocator_t allocator) {
  neo_ast_statement_for_await_of_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_for_await_of);
  node->node.type = NEO_NODE_TYPE_STATEMENT_FOR_AWAIT_OF;
  node->left = NULL;
  node->right = NULL;
  node->body = NULL;
  node->kind = NEO_AST_DECLARATION_NONE;
  return node;
}
neo_ast_node_t neo_ast_read_statement_for_await_of(neo_allocator_t allocator,
                                                   const char *file,
                                                   neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_statement_for_await_of_t node =
      neo_create_ast_statement_for_await_of(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "for")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "await")) {
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
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
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
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}
