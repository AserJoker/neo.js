#include "compiler/ast/object_property.h"
#include "compiler/asm.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "compiler/ast/object_key.h"
#include "compiler/program.h"
#include "compiler/writer.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_object_property_dispose(neo_allocator_t allocator,
                                            neo_ast_object_property_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}

static void
neo_ast_object_property_resolve_closure(neo_allocator_t allocator,
                                        neo_ast_object_property_t self,
                                        neo_list_t closure) {
  if (self->value) {
    self->value->resolve_closure(allocator, self->value, closure);
  } else {
    self->identifier->resolve_closure(allocator, self->identifier, closure);
  }
  if (self->computed) {
    self->identifier->resolve_closure(allocator, self->identifier, closure);
  }
}

static void neo_ast_object_property_write(neo_allocator_t allocator,
                                          neo_write_context_t ctx,
                                          neo_ast_object_property_t self) {
  if (self->computed) {
    TRY(self->identifier->write(allocator, ctx, self->identifier)) { return; }
  } else {
    if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
      neo_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
      char *name = neo_location_get(allocator, self->identifier->location);
      neo_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
    } else {
      TRY(self->identifier->write(allocator, ctx, self->identifier)) { return; }
    }
  }
  if (self->value) {
    self->value->write(allocator, ctx, self->value);
  } else if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    neo_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
    char *name = neo_location_get(allocator, self->identifier->location);
    neo_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  } else {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
          ctx->program->filename, self->identifier->location.begin.line,
          self->identifier->location.begin.column);
    return;
  }
  neo_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
}

static neo_variable_t
neo_serialize_ast_object_property(neo_allocator_t allocator,
                                  neo_ast_object_property_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_OBJECT_PROPERTY"));
  neo_variable_set(variable, L"identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, L"value",
                   neo_ast_node_serialize(allocator, node->value));
  neo_variable_set(variable, L"computed",
                   neo_create_variable_boolean(allocator, node->computed));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_object_property_t
neo_create_ast_object_property(neo_allocator_t allocator) {
  neo_ast_object_property_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_object_property_t),
                          neo_ast_object_property_dispose);
  node->identifier = NULL;
  node->value = NULL;
  node->node.type = NEO_NODE_TYPE_OBJECT_PROPERTY;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_object_property;
  node->node.resolve_closure =
      (neo_resolve_closure_fn_t)neo_ast_object_property_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_object_property_write;
  node->computed = false;
  return node;
}

neo_ast_node_t neo_ast_read_object_property(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_object_property_t node = neo_create_ast_object_property(allocator);
  node->identifier = TRY(neo_ast_read_object_key(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_object_computed_key(allocator, file, &current)) {
      goto onerror;
    }
    node->computed = true;
  }
  if (!node->identifier) {
    THROW("Invalid or unexpected token \n  at _.compile (%s:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ':') {
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
  };
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}