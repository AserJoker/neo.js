#include "neo.js/compiler/ast_object_property.h"
#include "neo.js/compiler/asm.h"
#include "neo.js/compiler/ast_expression.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/ast_object_key.h"
#include "neo.js/compiler/program.h"
#include "neo.js/compiler/writer.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/any.h"
#include "neo.js/core/location.h"
#include "neo.js/core/position.h"
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
    self->identifier->write(allocator, ctx, self->identifier);
  } else {
    if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
      neo_js_program_add_code(allocator, ctx->program, NEO_ASM_PUSH_STRING);
      char *name = neo_location_get(allocator, self->identifier->location);
      neo_js_program_add_string(allocator, ctx->program, name);
      neo_allocator_free(allocator, name);
    } else {
      self->identifier->write(allocator, ctx, self->identifier);
    }
  }
  if (self->value) {
    self->value->write(allocator, ctx, self->value);
  } else if (self->identifier->type == NEO_NODE_TYPE_IDENTIFIER) {
    neo_js_program_add_code(allocator, ctx->program, NEO_ASM_LOAD);
    char *name = neo_location_get(allocator, self->identifier->location);
    neo_js_program_add_string(allocator, ctx->program, name);
    neo_allocator_free(allocator, name);
  }
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_SET_FIELD);
}

static neo_any_t
neo_serialize_ast_object_property(neo_allocator_t allocator,
                                  neo_ast_object_property_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_OBJECT_PROPERTY"));
  neo_any_set(variable, "identifier",
              neo_ast_node_serialize(allocator, node->identifier));
  neo_any_set(variable, "value",
              neo_ast_node_serialize(allocator, node->value));
  neo_any_set(variable, "computed",
              neo_create_any_boolean(allocator, node->computed));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
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
  neo_ast_node_t error = NULL;
  neo_ast_object_property_t node = neo_create_ast_object_property(allocator);
  node->identifier = neo_ast_read_object_key(allocator, file, &current);
  if (!node->identifier) {
    node->identifier =
        neo_ast_read_object_computed_key(allocator, file, &current);
    node->computed = true;
  }
  if (!node->identifier) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node->identifier, error, onerror);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != ':') {
    goto onerror;
  }
  current.offset++;
  current.column++;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->value = neo_ast_read_expression_2(allocator, file, &current);
  if (node->value) {
    NEO_CHECK_NODE(node->value, error, onerror);
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return error;
}