#include "compiler/expression_function.h"
#include "compiler/function_argument.h"
#include "compiler/function_body.h"
#include "compiler/identifier.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>

static void
neo_ast_expression_function_dispose(neo_allocator_t allocator,
                                    neo_ast_expression_function_t node) {
  neo_allocator_free(allocator, node->arguments);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->name);
}

static neo_ast_expression_function_t
neo_create_ast_expression_function(neo_allocator_t allocator, const char *file,
                                   neo_position_t *position) {
  neo_ast_expression_function_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_function);
  node->async = false;
  node->generator = false;
  node->body = NULL;
  node->name = NULL;
  neo_list_initialize_t initialize = {true};
  node->arguments = neo_create_list(allocator, &initialize);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_FUNCTION;
  return node;
}

neo_ast_node_t neo_ast_read_expression_function(neo_allocator_t allocator,
                                                const char *file,
                                                neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_function_t node = NULL;
  neo_token_t token = NULL;
  node = neo_create_ast_expression_function(allocator, file, &current);
  token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  }
  if (!token || (!neo_location_is(token->location, "async") &&
                 !neo_location_is(token->location, "function"))) {
    goto onerror;
  }
  if (neo_location_is(token->location, "async")) {
    node->async = true;
    neo_allocator_free(allocator, token);
    SKIP_ALL(allocator, file, &current, onerror);
    token = TRY(neo_read_identify_token(allocator, file, &current)) {
      goto onerror;
    }
    if (!token || !neo_location_is(token->location, "function")) {
      goto onerror;
    }
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset == '*') {
    node->generator = true;
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
  }
  if (*current.offset != '(') {
    node->name = TRY(neo_ast_read_identifier(allocator, file, &current)) {
      goto onerror;
    };
    SKIP_ALL(allocator, file, &current, onerror);
  }
  if (*current.offset != '(') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ')') {
    for (;;) {
      neo_ast_node_t argument =
          TRY(neo_ast_read_function_argument(allocator, file, &current)) {
        goto onerror;
      }
      if (!argument) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      neo_list_push(node->arguments, argument);
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset == ')') {
        break;
      }
      if (*current.offset != ',') {
        goto onerror;
      }
      if (((neo_ast_function_argument_t)argument)->identifier->type ==
          NEO_NODE_TYPE_PATTERN_REST) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      current.offset++;
      current.column++;
      SKIP_ALL(allocator, file, &current, onerror);
    }
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->body = TRY(neo_ast_read_function_body(allocator, file, &current)) {
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