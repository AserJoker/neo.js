#include "compiler/ast/pattern_array.h"
#include "compiler/asm.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array_item.h"
#include "compiler/ast/pattern_rest.h"
#include "compiler/program.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/position.h"
#include "core/variable.h"

static void neo_ast_pattern_array_dispose(neo_allocator_t allocator,
                                          neo_ast_pattern_array_t node) {
  neo_allocator_free(allocator, node->items);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_pattern_array_write(neo_allocator_t allocator,
                                        neo_write_context_t ctx,
                                        neo_ast_pattern_array_t self) {
  neo_program_add_code(allocator, ctx->program, NEO_ASM_ITERATOR);
  for (neo_list_node_t it = neo_list_get_first(self->items);
       it != neo_list_get_tail(self->items); it = neo_list_node_next(it)) {
    neo_ast_node_t item = neo_list_node_get(it);
    if (item) {
      TRY(item->write(allocator, ctx, item)) { return; }
    } else {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_NEXT);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    }
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
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

static neo_variable_t
neo_serialize_ast_pattern_array(neo_allocator_t allocator,
                                neo_ast_pattern_array_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_PATTERN_ARRAY"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"items",
                   neo_ast_node_list_serialize(allocator, node->items));
  return variable;
}

static neo_ast_pattern_array_t
neo_create_ast_pattern_array(neo_allocator_t allocator) {
  neo_ast_pattern_array_t node =
      neo_allocator_alloc2(allocator, neo_ast_pattern_array);
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
                                          const wchar_t *file,
                                          neo_position_t *position) {
  neo_ast_pattern_array_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  if (*current.offset != '[') {
    return NULL;
  }
  current.offset++;
  current.column++;
  node = neo_create_ast_pattern_array(allocator);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ']') {
    for (;;) {
      neo_ast_node_t item =
          TRY(neo_ast_read_pattern_array_item(allocator, file, &current)) {
        goto onerror;
      }
      if (!item) {
        item = TRY(neo_ast_read_pattern_rest(allocator, file, &current)) {
          goto onerror;
        }
      }
      neo_list_push(node->items, item);
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset == ',') {
        current.offset++;
        current.column++;
        SKIP_ALL(allocator, file, &current, onerror);
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
  SKIP_ALL(allocator, file, &current, onerror);
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
  return NULL;
}