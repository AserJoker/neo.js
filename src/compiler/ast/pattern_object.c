#include "compiler/ast/pattern_object.h"
#include "compiler/asm.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_object_item.h"
#include "compiler/ast/pattern_rest.h"
#include "compiler/program.h"
#include "compiler/scope.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/position.h"
#include "core/variable.h"
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
  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_ARRAY);
  neo_program_add_number(allocator, ctx->program, 0);
  size_t idx = 0;
  for (neo_list_node_t it = neo_list_get_first(self->items);
       it != neo_list_get_tail(self->items); it = neo_list_node_next(it)) {
    neo_ast_node_t item = neo_list_node_get(it);
    if (item->type == NEO_NODE_TYPE_PATTERN_OBJECT_ITEM) {
      neo_ast_pattern_object_item_t oitem = (neo_ast_pattern_object_item_t)item;
      if (oitem->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
        wchar_t *name =
            neo_location_get(allocator, oitem->identifier->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else if (oitem->identifier->type == NEO_NODE_TYPE_LITERAL_STRING) {
        wchar_t *name =
            neo_location_get(allocator, oitem->identifier->location);
        name[wcslen(name) - 1] = 0;
        neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
        neo_program_add_string(allocator, ctx->program, name + 1);
        neo_allocator_free(allocator, name);
      } else {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)",
              ctx->program->filename, oitem->identifier->location.begin.line,
              oitem->identifier->location.begin.column);
        return;
      }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_program_add_integer(allocator, ctx->program, 2);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_NUMBER);
      neo_program_add_number(allocator, ctx->program, idx);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_program_add_integer(allocator, ctx->program, 3);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_program_add_integer(allocator, ctx->program, 3);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
      neo_program_add_integer(allocator, ctx->program, 2);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
      if (oitem->value) {
        neo_program_add_code(allocator, ctx->program, NEO_ASM_JNOT_NULL);
        size_t address = neo_buffer_get_size(ctx->program->codes);
        neo_program_add_address(allocator, ctx->program, 0);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
        TRY(oitem->value->write(allocator, ctx, oitem->value)) { return; }
        neo_program_set_current(ctx->program, address);
      }
      if (oitem->alias) {
        if (oitem->alias->type == NEO_NODE_TYPE_IDENTIFIER) {
          wchar_t *name = neo_location_get(allocator, oitem->alias->location);
          neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
          neo_program_add_string(allocator, ctx->program, name);
          neo_allocator_free(allocator, name);
        } else {
          TRY(oitem->alias->write(allocator, ctx, oitem->alias)) { return; }
        }
      } else if (oitem->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
        wchar_t *name =
            neo_location_get(allocator, oitem->identifier->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else {
        THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)",
              ctx->program->filename, oitem->identifier->location.begin.line,
              oitem->identifier->location.begin.column);
        return;
      }
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    } else if (item->type == NEO_NODE_TYPE_PATTERN_REST) {
      neo_ast_pattern_rest_t rest = (neo_ast_pattern_rest_t)item;
      neo_program_add_code(allocator, ctx->program, NEO_ASM_REST_OBJECT);
      if (rest->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
        wchar_t *name = neo_location_get(allocator, rest->identifier->location);
        neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
        neo_program_add_string(allocator, ctx->program, name);
        neo_allocator_free(allocator, name);
      } else {
        TRY(rest->identifier->write(allocator, ctx, rest->identifier)) {
          return;
        }
      }
    }
    idx++;
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
}

static neo_variable_t
neo_serialize_ast_pattern_object(neo_allocator_t allocator,
                                 neo_ast_pattern_object_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_PATTERN_OBJECT"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"items",
                   neo_ast_node_list_serialize(allocator, node->items));
  return variable;
}
static neo_ast_pattern_object_t
neo_create_ast_pattern_object(neo_allocator_t allocator) {
  neo_ast_pattern_object_t node =
      neo_allocator_alloc2(allocator, neo_ast_pattern_object);
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
                                           const wchar_t *file,
                                           neo_position_t *position) {
  neo_ast_pattern_object_t node = NULL;
  neo_token_t token = NULL;
  neo_position_t current = *position;
  if (*current.offset != '{') {
    return NULL;
  }
  current.offset++;
  current.column++;
  node = neo_create_ast_pattern_object(allocator);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '}') {
    for (;;) {
      neo_ast_node_t item =
          TRY(neo_ast_read_pattern_object_item(allocator, file, &current)) {
        goto onerror;
      };
      if (!item) {
        item = TRY(neo_ast_read_pattern_rest(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        goto onerror;
      }
      neo_list_push(node->items, item);
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset == ',') {
        current.offset++;
        current.column++;
        SKIP_ALL(allocator, file, &current, onerror);
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
  return NULL;
}