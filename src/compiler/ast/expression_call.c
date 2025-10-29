#include "compiler/ast/expression_call.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/expression_spread.h"
#include "compiler/ast/node.h"
#include "compiler/program.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/error.h"
#include "core/list.h"
#include "core/position.h"
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
  TRY(neo_write_optional_chain(allocator, ctx, &self->node, addresses)) {
    neo_allocator_free(allocator, addresses);
    return;
  }
  for (neo_list_node_t it = neo_list_get_first(addresses);
       it != neo_list_get_tail(addresses); it = neo_list_node_next(it)) {
    size_t *address = neo_list_node_get(it);
    neo_program_set_current(ctx->program, *address);
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
        THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
              current.line, current.column);
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
        THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
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
  return NULL;
}