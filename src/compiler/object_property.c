#include "compiler/object_property.h"
#include "compiler/expression.h"
#include "compiler/node.h"
#include "compiler/object_key.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_object_property_dispose(neo_allocator_t allocator,
                                            neo_ast_object_property_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->value);
}

static neo_variable_t
neo_serialize_ast_object_property(neo_allocator_t allocator,
                                  neo_ast_object_property_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_OBJECT_PROPERTY"));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  neo_variable_set(variable, "computed",
                   neo_create_variable_boolean(allocator, node->computed));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  return variable;
}

static neo_ast_object_property_t
neo_create_ast_object_property(neo_allocator_t allocator) {
  neo_ast_object_property_t node =
      neo_allocator_alloc2(allocator, neo_ast_object_property);
  node->identifier = NULL;
  node->value = NULL;
  node->node.type = NEO_NODE_TYPE_OBJECT_PROPERTY;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_object_property;
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
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
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