#include "compiler/literal_string.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/variable.h"

static void neo_ast_literal_string_dispose() {};

static neo_variable_t
neo_serialize_ast_literal_string(neo_allocator_t allocator,
                                 neo_ast_literal_string_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_LITERAL_STRING"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  return variable;
}

static neo_ast_literal_string_t
neo_create_string_litreral(neo_allocator_t allocator) {
  neo_ast_literal_string_t node =
      neo_allocator_alloc2(allocator, neo_ast_literal_string);
  node->node.type = NEO_NODE_TYPE_LITERAL_STRING;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_literal_string;
  return node;
}

neo_ast_node_t neo_ast_read_literal_string(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_literal_string_t node = NULL;
  neo_token_t token = TRY(neo_read_string_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token) {
    return NULL;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_string_litreral(allocator);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}