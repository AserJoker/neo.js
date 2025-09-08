#include "compiler/ast/pattern_array_item.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/node.h"
#include "compiler/ast/pattern_array.h"
#include "compiler/ast/pattern_object.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/buffer.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
static void
neo_ast_pattern_array_item_dispose(neo_allocator_t allocator,
                                   neo_ast_pattern_array_item_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_pattern_array_item_resolve_closure(neo_allocator_t allocator,
                                           neo_ast_pattern_array_item_t self,
                                           neo_list_t closure) {
  if (self->value) {
    self->value->resolve_closure(allocator, self->value, closure);
  }
  self->identifier->resolve_closure(allocator, self->identifier, closure);
}

static void
neo_ast_pattern_array_item_write(neo_allocator_t allocator,
                                 neo_write_context_t ctx,
                                 neo_ast_pattern_array_item_t self) {
  neo_program_add_code(allocator, ctx->program, NEO_ASM_NEXT);
  neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  if (self->value) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_JNOT_NULL);
    size_t address = neo_buffer_get_size(ctx->program->codes);
    neo_program_add_address(allocator, ctx->program, 0);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
    TRY(self->value->write(allocator, ctx, self->value)) { return; }
    neo_program_set_current(ctx->program, address);
  }
  if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    char *name = neo_location_get(allocator, self->identifier->location);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_STORE);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
    neo_program_add_code(allocator, ctx->program, NEO_ASM_POP);
  } else {
    TRY(self->identifier->write(allocator, ctx, self->identifier)) { return; }
  }
}

static neo_variable_t
neo_serialize_ast_pattern_array_item(neo_allocator_t allocator,
                                     neo_ast_pattern_array_item_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, L"type",
                   neo_create_variable_string(
                       allocator, L"NEO_NODE_TYPE_PATTERN_ARRAY_ITEM"));
  neo_variable_set(variable, L"identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, L"value",
                   neo_ast_node_serialize(allocator, node->value));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_pattern_array_item_t
neo_create_ast_pattern_array_item(neo_allocator_t allocator) {
  neo_ast_pattern_array_item_t node = neo_allocator_alloc(
      allocator, sizeof(struct _neo_ast_pattern_array_item_t),
      neo_ast_pattern_array_item_dispose);
  node->identifier = NULL;
  node->value = NULL;
  node->node.type = NEO_NODE_TYPE_PATTERN_ARRAY_ITEM;

  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_pattern_array_item;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_pattern_array_item_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_pattern_array_item_write;
  return node;
}

neo_ast_node_t neo_ast_read_pattern_array_item(neo_allocator_t allocator,
                                               const char *file,
                                               neo_position_t *position) {
  neo_ast_pattern_array_item_t node = NULL;
  neo_position_t current = *position;
  node = neo_create_ast_pattern_array_item(allocator);
  node->identifier = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->identifier) {
    node->identifier = neo_ast_read_pattern_array(allocator, file, &current);
  }
  if (!node->identifier) {
    node->identifier = neo_ast_read_pattern_object(allocator, file, &current);
  }
  if (!node->identifier) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset == '=') {
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
    node->value = neo_ast_read_expression_2(allocator, file, &current);
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