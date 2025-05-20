#include "compiler/literal_null.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"

static void neo_ast_literal_null_dispose() {}

static neo_variable_t
neo_serialize_ast_literal_null(neo_allocator_t allocator,
                               neo_ast_literal_null_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_LITERAL_NULL"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  return variable;
}

static neo_ast_literal_null_t
neo_create_ast_literal_null(neo_allocator_t allocator) {
  neo_ast_literal_null_t node =
      neo_allocator_alloc2(allocator, neo_ast_literal_null);
  node->node.type = NEO_NODE_TYPE_LITERAL_NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_literal_null;
  return node;
}

neo_ast_node_t neo_ast_read_literal_null(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_literal_null_t node = NULL;
  neo_token_t token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "null")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_literal_null(allocator);
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