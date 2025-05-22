#include "compiler/pattern_array_item.h"
#include "compiler/expression.h"
#include "compiler/identifier.h"
#include "compiler/node.h"
#include "compiler/pattern_array.h"
#include "compiler/pattern_object.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include "core/variable.h"
static void
neo_ast_pattern_array_item_dispose(neo_allocator_t allocator,
                                   neo_ast_pattern_array_item_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->value);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_pattern_array_item(neo_allocator_t allocator,
                                     neo_ast_pattern_array_item_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_PATTERN_ARRAY_ITEM"));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, "value",
                   neo_ast_node_serialize(allocator, node->value));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_pattern_array_item_t
neo_create_ast_pattern_array_item(neo_allocator_t allocator) {
  neo_ast_pattern_array_item_t node =
      neo_allocator_alloc2(allocator, neo_ast_pattern_array_item);
  node->identifier = NULL;
  node->value = NULL;
  node->node.type = NEO_NODE_TYPE_PATTERN_ARRAY_ITEM;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_pattern_array_item;
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