#include "compiler/import_attribute.h"
#include "compiler/identifier.h"
#include "compiler/literal_string.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_import_attribute_dispose(neo_allocator_t allocator,
                                             neo_ast_import_attribute_t node) {
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_import_attribute(neo_allocator_t allocator,
                                   neo_ast_import_attribute_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_IMPORT_SPECIFIER"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  return variable;
}

static neo_ast_import_attribute_t
neo_create_ast_import_attribute(neo_allocator_t allocator) {
  neo_ast_import_attribute_t node =
      neo_allocator_alloc2(allocator, neo_ast_import_attribute);
  node->node.type = NEO_NODE_TYPE_IMPORT_SPECIFIER;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_import_attribute;
  node->value = NULL;
  node->identifier = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_import_attribute(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_import_attribute_t node = neo_create_ast_import_attribute(allocator);
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ':') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->value = neo_ast_read_literal_string(allocator, file, &current);
  if (!node->value) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
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