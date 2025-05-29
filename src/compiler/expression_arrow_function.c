#include "compiler/expression_arrow_function.h"
#include "compiler/expression.h"
#include "compiler/function_argument.h"
#include "compiler/function_body.h"
#include "compiler/identifier.h"
#include "compiler/node.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_expression_arrow_function_dispose(
    neo_allocator_t allocator, neo_ast_expression_arrow_function_t node) {
  neo_allocator_free(allocator, node->closure);
  neo_allocator_free(allocator, node->arguments);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->node.scope);
  neo_allocator_free(allocator, node->closure);
}
static neo_variable_t neo_serialize_ast_expression_arrow_function(
    neo_allocator_t allocator, neo_ast_expression_arrow_function_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_EXPRESSION_ARROW_FUNCTION"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "arguments",
                   neo_ast_node_list_serialize(allocator, node->arguments));
  neo_variable_set(variable, "body",
                   neo_ast_node_serialize(allocator, node->body));
  neo_variable_set(variable, "closure",
                   neo_ast_node_list_serialize(allocator, node->closure));
  neo_variable_set(variable, "async",
                   neo_create_variable_boolean(allocator, node->async));
  return variable;
}
static neo_ast_expression_arrow_function_t
neo_create_ast_expression_arrow_function(neo_allocator_t allocator) {
  neo_ast_expression_arrow_function_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_arrow_function);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_ARROW_FUNCTION;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn)neo_serialize_ast_expression_arrow_function;
  neo_list_initialize_t initialize = {true};

  node->arguments = neo_create_list(allocator, &initialize);
  node->closure = neo_create_list(allocator, NULL);
  node->body = NULL;
  node->async = false;
  return node;
}

neo_ast_node_t neo_ast_read_expression_arrow_function(
    neo_allocator_t allocator, const char *file, neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_arrow_function_t node = NULL;
  neo_token_t token = NULL;
  neo_compile_scope_t scope = NULL;
  node = neo_create_ast_expression_arrow_function(allocator);

  token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  };

  if (token) {
    if (neo_location_is(token->location, "async")) {
      node->async = true;
    } else {
      current = token->location.begin;
    }
    neo_allocator_free(allocator, token);
  }

  SKIP_ALL(allocator, file, &current, onerror);

  if (*current.offset == '(') {
    current.offset++;
    current.column++;
    scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_FUNCTION);
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
        neo_resolve_closure(allocator, argument, node->closure);
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
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset != ')') {
      goto onerror;
    }
    current.offset++;
    current.column++;
  } else {
    scope = neo_compile_scope_push(allocator, NEO_COMPILE_SCOPE_FUNCTION);
    neo_ast_node_t argument =
        TRY(neo_ast_read_identifier(allocator, file, &current)) {
      goto onerror;
    }
    if (!argument) {
      goto onerror;
    }
    neo_list_push(node->arguments, argument);
    neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                             argument, NEO_COMPILE_VARIABLE_LET);
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '=' || *(current.offset + 1) != '>') {
    goto onerror;
  } else {
    current.offset += 2;
    current.column += 2;
  }
  SKIP_ALL(allocator, file, &current, onerror);

  if (*current.offset == '{') {
    node->body = TRY(neo_ast_read_function_body(allocator, file, &current)) {
      goto onerror;
    }
  } else {
    node->body = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
      goto onerror;
    };
  }

  if (!node->body) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  neo_resolve_closure(allocator, node->body, node->closure);
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