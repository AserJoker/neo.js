#include "compiler/expression_call.h"
#include "compiler/expression.h"
#include "compiler/expression_spread.h"
#include "compiler/node.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_expression_call_dispose(neo_allocator_t allocator,
                                            neo_ast_expression_call_t node) {
  neo_allocator_free(allocator, node->callee);
  neo_allocator_free(allocator, node->arguments);
}

static neo_variable_t
neo_serialize_ast_expression_assigment(neo_allocator_t allocator,
                                       neo_ast_expression_call_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_EXPRESSION_CALL"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "callee",
                   neo_ast_node_serialize(allocator, node->callee));
  neo_variable_set(variable, "arguments",
                   neo_ast_node_list_serialize(allocator, node->arguments));
  return variable;
}

static neo_ast_expression_call_t
neo_create_ast_expression_call(neo_allocator_t allocator) {
  neo_ast_expression_call_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_call);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_CALL;
  node->node.serialize =
      (neo_serialize_fn)neo_serialize_ast_expression_assigment;
  neo_list_initialize_t initialize = {true};
  node->callee = NULL;
  node->arguments = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_expression_call(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_call_t node = neo_create_ast_expression_call(allocator);
  if (*current.offset == '?' && *(current.offset + 1) == '.') {
    current.offset += 2;
    current.column += 2;
    SKIP_ALL(allocator, file, &current, onerror);
    node->node.type = NEO_NODE_TYPE_EXPRESSION_OPTIONAL_CALL;
  }
  if (*current.offset != '(') {
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ')') {
    for (;;) {
      neo_ast_node_t argument =
          TRY(neo_ast_read_expression_2(allocator, file, &current)) {
        goto onerror;
      }
      if (!argument) {
        argument =
            TRY(neo_ast_read_expression_spread(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!argument) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      neo_list_push(node->arguments, argument);
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset == ',') {
        current.offset++;
        current.column++;
        SKIP_ALL(allocator, file, &current, onerror);
      } else if (*current.offset == ')') {
        break;
      } else {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
    }
  }
  current.offset++;
  current.column++;
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}