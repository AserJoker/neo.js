#include "compiler/literal_boolean.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"

static void neo_ast_literal_boolean_dispose(neo_allocator_t allocator,
                                            neo_ast_literal_boolean_t node) {
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_literal_boolean(neo_allocator_t allocator,
                                  neo_ast_literal_boolean_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_LITERAL_BOOLEAN"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  return variable;
}

static neo_ast_literal_boolean_t
neo_create_ast_literal_boolean(neo_allocator_t allocator) {
  neo_ast_literal_boolean_t node =
      neo_allocator_alloc2(allocator, neo_ast_literal_boolean);
  node->node.type = NEO_NODE_TYPE_LITERAL_BOOLEAN;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_literal_boolean;
  return node;
}

neo_ast_node_t neo_ast_read_literal_boolean(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_literal_boolean_t node = NULL;
  neo_token_t token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  }
  if (!token || (!neo_location_is(token->location, "true") &&
                 !neo_location_is(token->location, "false"))) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_literal_boolean(allocator);
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