#include "compiler/asm.h"
#include "compiler/ast_identifier.h"
#include "compiler/ast_import_attribute.h"
#include "compiler/ast_literal_string.h"
#include "compiler/ast_node.h"
#include "compiler/program.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/location.h"
#include <stdio.h>
#include <string.h>

static void neo_ast_import_attribute_dispose(neo_allocator_t allocator,
                                             neo_ast_import_attribute_t node) {
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->node.scope);
}

static void neo_ast_import_attribute_write(neo_allocator_t allocator,
                                           neo_write_context_t ctx,
                                           neo_ast_import_attribute_t self) {
  neo_js_program_add_code(allocator, ctx->program, NEO_ASM_ASSERT);
  char *name = neo_location_get(allocator, self->identifier->location);
  neo_js_program_add_string(allocator, ctx->program, name);
  neo_allocator_free(allocator, name);
  char *value = neo_location_get(allocator, self->value->location);
  value[strlen(value) - 1] = 0;
  neo_js_program_add_string(allocator, ctx->program, value + 1);
  neo_allocator_free(allocator, value);
}

static neo_any_t
neo_serialize_ast_import_attribute(neo_allocator_t allocator,
                                   neo_ast_import_attribute_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_IMPORT_SPECIFIER"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "value",
              neo_ast_node_serialize(allocator, node->value));
  neo_any_set(variable, "identifier",
              neo_ast_node_serialize(allocator, node->identifier));
  return variable;
}

static neo_ast_import_attribute_t
neo_create_ast_import_attribute(neo_allocator_t allocator) {
  neo_ast_import_attribute_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_import_attribute_t),
                          neo_ast_import_attribute_dispose);
  node->node.type = NEO_NODE_TYPE_IMPORT_SPECIFIER;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_import_attribute;
  node->value = NULL;
  node->identifier = NULL;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_import_attribute_write;
  return node;
}

neo_ast_node_t neo_ast_read_import_attribute(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_node_t error = NULL;
  neo_ast_import_attribute_t node = neo_create_ast_import_attribute(allocator);
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
    goto onerror;
  }

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != ':') {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->value = neo_ast_read_literal_string(allocator, file, &current);
  if (!node->value) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
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