#include "neo.js/compiler/ast_private_name.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/compiler/token.h"

static void neo_ast_private_name_dispose(neo_allocator_t allocator,
                                         neo_ast_private_name_t node) {
  neo_allocator_free(allocator, node->node.scope);
}
static neo_any_t neo_serialize_ast_private_name(neo_allocator_t allocator,
                                                neo_ast_private_name_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_PRIVATE_NAME"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  return variable;
}
static neo_ast_private_name_t
neo_create_ast_private_name(neo_allocator_t allocator) {
  neo_ast_private_name_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_private_name_t),
                          neo_ast_private_name_dispose);
  node->node.type = NEO_NODE_TYPE_PRIVATE_NAME;
  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_private_name;
  node->node.resolve_closure = NULL;
  node->node.write = NULL;
  return node;
}
neo_ast_node_t neo_ast_read_private_name(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_private_name_t node = NULL;
  neo_ast_node_t error = NULL;
  neo_token_t token = neo_read_identify_token(allocator, file, &current);
  if (!token) {
    return NULL;
  }
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (*token->location.begin.offset != '#') {
    neo_allocator_free(allocator, token);
    return NULL;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_private_name(allocator);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}