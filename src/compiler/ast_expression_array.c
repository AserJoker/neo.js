#include "neojs/compiler/ast_expression_array.h"
#include "neojs/compiler/asm.h"
#include "neojs/compiler/ast_expression.h"
#include "neojs/compiler/ast_expression_spread.h"
#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/program.h"
#include "neojs/compiler/writer.h"
#include "neojs/core/allocator.h"
#include "neojs/core/any.h"
#include "neojs/core/list.h"
#include "neojs/core/position.h"
#include <stdbool.h>

static void neo_ast_expression_array_dispose(neo_allocator_t allocator,
                                             neo_ast_expression_array_t node) {
  neo_allocator_free(allocator, node->items);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_expression_array_resolve_closure(neo_allocator_t allocator,
                                         neo_ast_expression_array_t self,
                                         neo_list_t closure) {
  for (neo_list_node_t it = neo_list_get_first(self->items);
       it != neo_list_get_tail(self->items); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    if (item) {
      item->resolve_closure(allocator, item, closure);
    }
  }
}

static neo_any_t
neo_serialize_ast_expression_array(neo_allocator_t allocator,
                                   neo_ast_expression_array_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_EXPRESSION_ARRAY"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "items",
              neo_ast_node_list_serialize(allocator, node->items));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static void neo_ast_expression_array_write(neo_allocator_t allocator,
                                           neo_write_context_t ctx,
                                           neo_ast_expression_array_t self) {
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_ARRAY);
  for (neo_list_node_t it = neo_list_get_first(self->items);
       it != neo_list_get_tail(self->items); it = neo_list_node_next(it)) {
    neo_ast_node_t item = neo_list_node_get(it);
    if (item) {
      if (item->type == NEO_NODE_TYPE_EXPRESSION_SPREAD) {
        item->write(allocator, ctx, item);
      } else {
        item->write(allocator, ctx, item);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_APPEND);
      }
    } else {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_APPEND_EMPTY);
    }
  }
}
static neo_ast_expression_array_t
neo_create_ast_expression_array(neo_allocator_t allocator) {
  neo_ast_expression_array_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_expression_array_t),
                          neo_ast_expression_array_dispose);
  neo_list_initialize_t initialize = {true};
  node->node.type = NEO_NODE_TYPE_EXPRESSION_ARRAY;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_expression_array;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_expression_array_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_expression_array_write;
  node->items = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_expression_array(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_node_t error = NULL;
  neo_ast_expression_array_t node = NULL;
  if (*current.offset != '[') {
    return NULL;
  }
  current.offset++;
  current.column++;
  node = neo_create_ast_expression_array(allocator);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != ']') {
    for (;;) {
      neo_ast_node_t item =
          neo_ast_read_expression_2(allocator, file, &current);
      if (!item) {
        item = neo_ast_read_expression_spread(allocator, file, &current);
      }
      if (item) {
        NEO_CHECK_NODE(item, error, onerror);
      }
      neo_list_push(node->items, item);
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
        if (*current.offset == ']') {
          break;
        }
      } else if (*current.offset == ']') {
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
  current.column++;
  current.offset++;
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return error;
}