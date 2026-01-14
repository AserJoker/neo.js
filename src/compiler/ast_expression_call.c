#include "neo.js/compiler/ast_expression_call.h"
#include "neo.js/compiler/ast_expression.h"
#include "neo.js/compiler/ast_expression_spread.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/program.h"
#include "neo.js/compiler/writer.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/any.h"
#include "neo.js/core/list.h"
#include "neo.js/core/position.h"
#include <stdbool.h>
#include <stdio.h>

static void neo_ast_expression_call_dispose(neo_allocator_t allocator,
                                            neo_ast_expression_call_t node) {
  neo_allocator_free(allocator, node->callee);
  neo_allocator_free(allocator, node->arguments);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_expression_call_resolve_closure(neo_allocator_t allocator,
                                        neo_ast_expression_call_t self,
                                        neo_list_t closure) {
  self->callee->resolve_closure(allocator, self->callee, closure);
  for (neo_list_node_t it = neo_list_get_first(self->arguments);
       it != neo_list_get_tail(self->arguments); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
}

static void neo_ast_expression_call_write(neo_allocator_t allocator,
                                          neo_write_context_t ctx,
                                          neo_ast_expression_call_t self) {
  neo_list_initialize_t initialize = {true};
  neo_list_t addresses = neo_create_list(allocator, &initialize);
  neo_write_optional_chain(allocator, ctx, &self->node, addresses);
  for (neo_list_node_t it = neo_list_get_first(addresses);
       it != neo_list_get_tail(addresses); it = neo_list_node_next(it)) {
    size_t *address = neo_list_node_get(it);
    neo_js_program_set_current(ctx->program, *address);
  }
  neo_allocator_free(allocator, addresses);
}

static neo_any_t
neo_serialize_ast_expression_assigment(neo_allocator_t allocator,
                                       neo_ast_expression_call_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_EXPRESSION_CA"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "callee",
              neo_ast_node_serialize(allocator, node->callee));
  neo_any_set(variable, "arguments",
              neo_ast_node_list_serialize(allocator, node->arguments));
  return variable;
}

static neo_ast_expression_call_t
neo_create_ast_expression_call(neo_allocator_t allocator) {
  neo_ast_expression_call_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_expression_call_t),
                          neo_ast_expression_call_dispose);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_CALL;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_expression_assigment;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_call_resolve_closure;
  neo_list_initialize_t initialize = {true};
  node->callee = NULL;
  node->arguments = neo_create_list(allocator, &initialize);
  node->node.write = (neo_write_fn_t)neo_ast_expression_call_write;
  return node;
}

neo_ast_node_t neo_ast_read_expression_call(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_ast_node_t error = NULL;
  neo_position_t current = *position;
  neo_ast_expression_call_t node = neo_create_ast_expression_call(allocator);
  if (*current.offset == '?' && *(current.offset + 1) == '.') {
    current.offset += 2;
    current.column += 2;

    error = neo_skip_all(allocator, file, &current);
    if (error) {
      goto onerror;
    }
    node->node.type = NEO_NODE_TYPE_EXPRESSION_OPTIONAL_CALL;
  }
  if (*current.offset != '(') {
    goto onerror;
  }
  current.offset++;
  current.column++;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != ')') {
    for (;;) {
      neo_ast_node_t argument =
          neo_ast_read_expression_2(allocator, file, &current);
      if (!argument) {
        argument = neo_ast_read_expression_spread(allocator, file, &current);
      }
      if (!argument) {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
        goto onerror;
      }
      NEO_CHECK_NODE(argument, error, onerror);
      neo_list_push(node->arguments, argument);
      error = neo_skip_all(allocator, file, &current);
      if (error) {
        goto onerror;
      }
      if (*current.offset == ',') {
        current.offset++;
        current.column++;

        error = neo_skip_all(allocator, file, &current);
        if (error) {
          goto onerror;
        }
      } else if (*current.offset == ')') {
        break;
      } else {
        error = neo_create_error_node(
            allocator,
            "Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
            current.line, current.column);
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
  return error;
}