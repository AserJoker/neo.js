#include "neojs/compiler/ast_pattern_array.h"
#include "neojs/compiler/asm.h"
#include "neojs/compiler/ast_node.h"
#include "neojs/compiler/ast_pattern_array_item.h"
#include "neojs/compiler/ast_pattern_rest.h"
#include "neojs/compiler/program.h"
#include "neojs/compiler/token.h"
#include "neojs/core/allocator.h"
#include "neojs/core/any.h"
#include "neojs/core/list.h"
#include "neojs/core/position.h"

static void neo_ast_pattern_array_dispose(neo_allocator_t allocator,
                                          neo_ast_pattern_array_t node) {
  neo_allocator_free(allocator, node->items);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_pattern_array_write(neo_allocator_t allocator,
                                        neo_write_context_t ctx,
                                        neo_ast_pattern_array_t self) {
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_ITERATOR);
  for (neo_list_node_t it = neo_list_get_first(self->items);
       it != neo_list_get_tail(self->items); it = neo_list_node_next(it)) {
    neo_ast_node_t item = neo_list_node_get(it);
    if (item) {
      item->write(allocator, ctx, item);
    } else {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_NEXT);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_RESOLVE_NEXT);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    }
  }
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
}

static void neo_ast_pattern_array_resolve_closure(neo_allocator_t allocator,
                                                  neo_ast_pattern_array_t self,
                                                  neo_list_t closure) {
  for (neo_list_node_t it = neo_list_get_first(self->items);
       it != neo_list_get_tail(self->items); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    if (item) {
      item->resolve_closure(allocator, item, closure);
    }
  }
}

static neo_any_t neo_serialize_ast_pattern_array(neo_allocator_t allocator,
                                                 neo_ast_pattern_array_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_PATTERN_ARRAY"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "items",
              neo_ast_node_list_serialize(allocator, node->items));
  return variable;
}

static neo_ast_pattern_array_t
neo_create_ast_pattern_array(neo_allocator_t allocator) {
  neo_ast_pattern_array_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_pattern_array_t),
                          neo_ast_pattern_array_dispose);
  neo_list_initialize_t initialize = {true};
  node->items = neo_create_list(allocator, &initialize);
  node->node.type = NEO_NODE_TYPE_PATTERN_ARRAY;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_pattern_array;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_pattern_array_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_pattern_array_write;
  return node;
}

neo_ast_node_t neo_ast_read_pattern_array(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position) {
  neo_ast_pattern_array_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  neo_ast_node_t error = NULL;
  if (*current.offset != '[') {
    return NULL;
  }
  current.offset++;
  current.column++;
  node = neo_create_ast_pattern_array(allocator);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != ']') {
    for (;;) {
      neo_ast_node_t item =
          neo_ast_read_pattern_array_item(allocator, file, &current);
      if (!item) {
        item = neo_ast_read_pattern_rest(allocator, file, &current);
      }
      NEO_CHECK_NODE(item, error, onerror);
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
        goto onerror;
      }
    }
  }

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != ']') {
    goto onerror;
  } else {
    current.offset++;
    current.column++;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return error;
}