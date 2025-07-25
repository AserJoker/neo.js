#include "compiler/ast/pattern_object_item.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/literal_numeric.h"
#include "compiler/ast/literal_string.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array.h"
#include "compiler/ast/pattern_object.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <string.h>
static void
neo_ast_pattern_object_item_dispose(neo_allocator_t allocator,
                                    neo_ast_pattern_object_item_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->alias);
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}
static void
neo_ast_pattern_object_item_write(neo_allocator_t allocator,
                                  neo_write_context_t ctx,
                                  neo_ast_pattern_object_item_t self) {

  neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_VALUE);
  neo_program_add_integer(allocator, ctx->program, 1);
  if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    wchar_t *name = neo_location_get(allocator, self->identifier->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  } else if (self->identifier->type == NEO_NODE_TYPE_LITERAL_STRING) {
    wchar_t *name = neo_location_get(allocator, self->identifier->location);
    name[wcslen(name) - 1] = 0;
    neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
    neo_program_add_string(allocator, ctx->program, name + 1);
    neo_allocator_free(allocator, name);
  } else {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)",
          ctx->program->filename, self->identifier->location.begin.line,
          self->identifier->location.begin.column);
    return;
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_GET_FIELD);
  if (self->value) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_JNOT_NULL);
    size_t address = neo_buffer_get_size(ctx->program->codes);
    neo_program_add_address(allocator, ctx->program, 0);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    TRY(self->value->write(allocator, ctx, self->value)) { return; }
    neo_program_set_current(ctx->program, address);
  }
  if (self->alias) {
    if (self->alias->type == NEO_NODE_TYPE_IDENTIFIER) {
      wchar_t *name = neo_location_get(allocator, self->alias->location);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
      neo_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
      neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    } else {
      TRY(self->alias->write(allocator, ctx, self->alias)) { return; }
    }
  } else if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    wchar_t *name = neo_location_get(allocator, self->identifier->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  } else {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)",
          ctx->program->filename, self->identifier->location.begin.line,
          self->identifier->location.begin.column);
    return;
  }
}
static void
neo_ast_pattern_object_item_resolve_closure(neo_allocator_t allocator,
                                            neo_ast_pattern_object_item_t self,
                                            neo_list_t closure) {
  if (self->value) {
    self->value->resolve_closure(allocator, self->value, closure);
  }
  if (!self->alias) {
    self->identifier->resolve_closure(allocator, self->identifier, closure);
  }
}

static neo_variable_t
neo_serialize_ast_pattern_object_item(neo_allocator_t allocator,
                                      neo_ast_pattern_object_item_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, L"type",
                   neo_create_variable_string(
                       allocator, L"NEO_NODE_TYPE_PATTERN_OBJECT_ITEM"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, L"identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, L"value",
                   neo_ast_node_serialize(allocator, node->value));
  neo_variable_set(variable, L"alias",
                   neo_ast_node_serialize(allocator, node->alias));
  return variable;
}

static neo_ast_pattern_object_item_t
neo_create_ast_pattern_object_item(neo_allocator_t allocator) {
  neo_ast_pattern_object_item_t node =
      neo_allocator_alloc2(allocator, neo_ast_pattern_object_item);
  node->node.type = NEO_NODE_TYPE_PATTERN_OBJECT_ITEM;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_pattern_object_item;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_pattern_object_item_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_pattern_object_item_write;
  node->identifier = NULL;
  node->alias = NULL;
  node->value = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_pattern_object_item(neo_allocator_t allocator,
                                                const wchar_t *file,
                                                neo_position_t *position) {
  neo_ast_pattern_object_item_t node = NULL;
  neo_position_t current = *position;
  node = neo_create_ast_pattern_object_item(allocator);
  node->identifier = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_literal_numeric(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_literal_string(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset == ':') {
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
    node->alias = TRY(neo_ast_read_identifier(allocator, file, &current)) {
      goto onerror;
    }
    if (!node->alias) {
      node->alias = TRY(neo_ast_read_pattern_array(allocator, file, &current)) {
        goto onerror;
      }
    }
    if (!node->alias) {
      node->alias =
          TRY(neo_ast_read_pattern_object(allocator, file, &current)) {
        goto onerror;
      }
    }
    if (!node->alias) {
      goto onerror;
    }
    SKIP_ALL(allocator, file, &current, onerror);
  }
  if (*current.offset == '=') {
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
    node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
      goto onerror;
    };
    if (!node->value) {
      goto onerror;
    }
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