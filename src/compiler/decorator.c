#include "compiler/decorator.h"
#include "compiler/expression_call.h"
#include "compiler/expression_group.h"
#include "compiler/expression_member.h"
#include "compiler/identifier.h"
#include "compiler/node.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>
static void neo_ast_decorator_dispose(neo_allocator_t allocator,
                                      neo_ast_decorator_t node) {
  neo_allocator_free(allocator, node->callee);
  neo_allocator_free(allocator, node->arguments);
}

static neo_variable_t neo_serialize_ast_decorator(neo_allocator_t allocator,
                                                  neo_ast_decorator_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_DECORATOR"));
  neo_variable_set(variable, "callee",
                   neo_ast_node_serialize(allocator, node->callee));
  neo_variable_set(variable, "arguments",
                   neo_ast_node_list_serialize(allocator, node->arguments));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  return variable;
}

static neo_ast_decorator_t neo_create_ast_decorator(neo_allocator_t allocator) {
  neo_ast_decorator_t node = neo_allocator_alloc2(allocator, neo_ast_decorator);
  node->node.type = NEO_NODE_TYPE_DECORATOR;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_decorator;
  node->callee = NULL;
  neo_list_initialize_t intialize = {true};
  node->arguments = neo_create_list(allocator, &intialize);
  return node;
}

neo_ast_node_t neo_ast_read_decorator(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_decorator_t node = NULL;
  if (*current.offset != '@') {
    return NULL;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node = neo_create_ast_decorator(allocator);
  node->callee = TRY(neo_ast_read_expression_group(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->callee) {
    node->callee = TRY(neo_ast_read_identifier(allocator, file, &current)) {
      goto onerror;
    };
  }
  if (!node->callee) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  for (;;) {
    neo_ast_expression_member_t item = (neo_ast_expression_member_t)TRY(
        neo_ast_read_expression_member(allocator, file, &cur)) {
      goto onerror;
    }
    if (!item) {
      break;
    }
    item->host = node->callee;
    node->callee = &item->node;
    item->node.location.begin = *position;
    SKIP_ALL(allocator, file, &cur, onerror);
    current = cur;
  }
  if (*current.offset == '(') {
    neo_ast_expression_call_t call = (neo_ast_expression_call_t)TRY(
        neo_ast_read_expression_call(allocator, file, &current)) {
      goto onerror;
    }
    if (!call) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    neo_allocator_free(allocator, node->arguments);
    node->arguments = call->arguments;
    call->arguments = NULL;
    neo_allocator_free(allocator, call);
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}
