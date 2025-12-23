#include "compiler/asm.h"
#include "compiler/ast_node.h"
#include "compiler/ast_pattern_object.h"
#include "compiler/ast_pattern_object_item.h"
#include "compiler/ast_pattern_rest.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/list.h"
#include "core/position.h"
#include <string.h>


static void neo_ast_pattern_object_dispose(neo_allocator_t allocator,
                                           neo_ast_pattern_object_t node) {
  neo_allocator_free(allocator, node->items);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_pattern_object_resolve_closure(neo_allocator_t allocator,
                                       neo_ast_pattern_object_t self,
                                       neo_list_t closure) {
  for (neo_list_node_t it = neo_list_get_first(self->items);
       it != neo_list_get_tail(self->items); it = neo_list_node_next(it)) {
    neo_ast_node_t item = (neo_ast_node_t)neo_list_node_get(it);
    item->resolve_closure(allocator, item, closure);
  }
}

static void neo_ast_pattern_object_write(neo_allocator_t allocator,
                                         neo_write_context_t ctx,
                                         neo_ast_pattern_object_t self) {
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_ARRAY);
  size_t idx = 0;
  for (neo_list_node_t it = neo_list_get_first(self->items);
       it != neo_list_get_tail(self->items); it = neo_list_node_next(it)) {
    neo_ast_node_t item = neo_list_node_get(it);
    if (item->type == NEO_NODE_TYPE_PATTERN_OBJECT_ITEM) {
      neo_ast_pattern_object_item_t oitem = (neo_ast_pattern_object_item_t)item;
      if (oitem->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
        char *name = neo_location_get(allocator, oitem->identifier->location);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_js_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else if (oitem->identifier->type == NEO_NODE_TYPE_LITERAL_STRING) {
        char *name = neo_location_get(allocator, oitem->identifier->location);
        name[strlen(name) - 1] = 0;
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_js_program_add_string(allocator, ctx->program, name + 1);
        neo_allocator_free(allocator, name);
      }
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_js_program_add_integer(allocator, ctx->program, 2);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_js_program_add_integer(allocator, ctx->program, 2);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_APPEND);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_js_program_add_integer(allocator, ctx->program, 3);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_js_program_add_integer(allocator, ctx->program, 2);
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
      if (oitem->value) {
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_JNOT_NULL);
        size_t address = neo_buffer_get_size(ctx->program->codes);
        neo_js_program_add_address(allocator, ctx->program, 0);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
        oitem->value->write(allocator, ctx, oitem->value);
        neo_js_program_set_current(ctx->program, address);
      }
      if (oitem->alias) {
        if (oitem->alias->type == NEO_NODE_TYPE_IDENTIFIER) {
          char *name = neo_location_get(allocator, oitem->alias->location);
          neo_js_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
          neo_js_program_add_string(allocator, ctx->program, name);
          neo_allocator_free(allocator, name);
          neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
        } else {
          oitem->alias->write(allocator, ctx, oitem->alias);
        }
      } else if (oitem->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
        char *name = neo_location_get(allocator, oitem->identifier->location);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
        neo_js_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      }
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    } else if (item->type == NEO_NODE_TYPE_PATTERN_REST) {
      neo_ast_pattern_rest_t rest = (neo_ast_pattern_rest_t)item;
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_REST_OBJECT);
      if (rest->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
        char *name = neo_location_get(allocator, rest->identifier->location);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
        neo_js_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
        neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      } else {
        rest->identifier->write(allocator, ctx, rest->identifier);
      }
    }
    idx++;
  }
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_POP);
}

static neo_any_t
neo_serialize_ast_pattern_object(neo_allocator_t allocator,
                                 neo_ast_pattern_object_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_PATTERN_OBJECT"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "items",
              neo_ast_node_list_serialize(allocator, node->items));
  return variable;
}
static neo_ast_pattern_object_t
neo_create_ast_pattern_object(neo_allocator_t allocator) {
  neo_ast_pattern_object_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_pattern_object_t),
                          neo_ast_pattern_object_dispose);
  neo_list_initialize_t initialize = {true};
  node->items = neo_create_list(allocator, &initialize);
  node->node.type = NEO_NODE_TYPE_PATTERN_OBJECT;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_pattern_object;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_pattern_object_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_pattern_object_write;
  return node;
}

neo_ast_node_t neo_ast_read_pattern_object(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_ast_pattern_object_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  neo_ast_node_t error = NULL;
  if (*current.offset != '{') {
    return NULL;
  }
  current.offset++;
  current.column++;
  node = neo_create_ast_pattern_object(allocator);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != '}') {
    for (;;) {
      neo_ast_node_t item =
          neo_ast_read_pattern_object_item(allocator, file, &current);
      if (!item) {
        item = neo_ast_read_pattern_rest(allocator, file, &current);
      }
      if (!item) {
        goto onerror;
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
      } else if (*current.offset == '}') {
        break;
      } else {
        goto onerror;
      }
    }
  }
  if (*current.offset != '}') {
    goto onerror;
  }
  current.offset++;
  current.column++;
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}