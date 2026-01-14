#include "neo.js/compiler/ast_decorator.h"
#include "neo.js/compiler/ast_expression_call.h"
#include "neo.js/compiler/ast_expression_group.h"
#include "neo.js/compiler/ast_expression_member.h"
#include "neo.js/compiler/ast_identifier.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/any.h"
#include "neo.js/core/list.h"
#include "neo.js/core/position.h"
#include <stdbool.h>
#include <stdio.h>

static void neo_ast_decorator_dispose(neo_allocator_t allocator,
                                      neo_ast_decorator_t node) {
  neo_allocator_free(allocator, node->callee);
  neo_allocator_free(allocator, node->arguments);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_decorator_resolve_closure(neo_allocator_t allocator,
                                              neo_ast_decorator_t node,
                                              neo_list_t closure) {
  node->callee->resolve_closure(allocator, node->callee, closure);
  for (neo_list_node_t it = neo_list_get_first(node->arguments);
       it != neo_list_get_tail(node->arguments); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
}

static neo_any_t neo_serialize_ast_decorator(neo_allocator_t allocator,
                                             neo_ast_decorator_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_DECORATOR"));
  neo_any_set(variable, "callee",
              neo_ast_node_serialize(allocator, node->callee));
  neo_any_set(variable, "arguments",
              neo_ast_node_list_serialize(allocator, node->arguments));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_decorator_t neo_create_ast_decorator(neo_allocator_t allocator) {
  neo_ast_decorator_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_decorator_t),
                          neo_ast_decorator_dispose);
  node->node.type = NEO_NODE_TYPE_DECORATOR;

  node->node.scope = NULL;
  node->node.write = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_decorator;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_decorator_resolve_closure;
  node->callee = NULL;
  neo_list_initialize_t intialize = {true};
  node->arguments = neo_create_list(allocator, &intialize);
  return node;
}

neo_ast_node_t neo_ast_read_decorator(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_node_t error = NULL;
  neo_ast_decorator_t node = NULL;
  if (*current.offset != '@') {
    return NULL;
  }
  current.offset++;
  current.column++;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node = neo_create_ast_decorator(allocator);
  node->callee = neo_ast_read_expression_group(allocator, file, &current);
  if (!node->callee) {
    node->callee = neo_ast_read_identifier(allocator, file, &current);
  }
  if (!node->callee) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->callee, error, onerror);
  neo_position_t cur = current;

  error = neo_skip_all(allocator, file, &cur);
  if (error) {
    goto onerror;
  }
  for (;;) {
    neo_ast_node_t expr = neo_ast_read_expression_member(allocator, file, &cur);
    if (!expr) {
      break;
    }
    NEO_CHECK_NODE(expr, error, onerror);
    neo_ast_expression_member_t item = (neo_ast_expression_member_t)expr;
    item->host = node->callee;
    node->callee = &item->node;
    item->node.location.begin = *position;
    error = neo_skip_all(allocator, file, &cur);
    if (error) {
      goto onerror;
    }
    current = cur;
  }
  if (*current.offset == '(') {
    neo_ast_node_t expr =
        neo_ast_read_expression_call(allocator, file, &current);
    if (!expr) {
      error = neo_create_error_node(
          allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          file, current.line, current.column);
      goto onerror;
    }
    NEO_CHECK_NODE(expr, error, onerror);
    neo_ast_expression_call_t call = (neo_ast_expression_call_t)expr;
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
  return error;
}
