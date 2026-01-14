#include "neo.js/compiler/ast_export_namespace.h"
#include "neo.js/compiler/ast_identifier.h"
#include "neo.js/compiler/scope.h"
#include "neo.js/compiler/token.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/any.h"
#include "neo.js/core/location.h"
#include "neo.js/core/position.h"
#include <stdio.h>

static void neo_ast_export_namespace_dispose(neo_allocator_t allocator,
                                             neo_ast_export_namespace_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_any_t
neo_serialize_ast_export_namespace(neo_allocator_t allocator,
                                   neo_ast_export_namespace_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(
      variable, "type",
      neo_create_any_string(allocator, "NEO_NODE_TYPE_EXPORT_NAMESPACE"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  neo_any_set(variable, "identifier",
              neo_ast_node_serialize(allocator, node->identifier));
  return variable;
}

static neo_ast_export_namespace_t
neo_create_ast_export_namespace(neo_allocator_t allocator) {
  neo_ast_export_namespace_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_export_namespace_t),
                          neo_ast_export_namespace_dispose);
  node->node.type = NEO_NODE_TYPE_EXPORT_NAMESPACE;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_export_namespace;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->identifier = NULL;
  node->node.write = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_export_namespace(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_node_t error = NULL;
  neo_ast_export_namespace_t node = neo_create_ast_export_namespace(allocator);
  neo_token_t token = NULL;
  if (*current.offset != '*') {
    goto onerror;
  }
  current.offset++;
  current.column++;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  token = neo_read_identify_token(allocator, file, &current);
  if (token && token->type == NEO_TOKEN_TYPE_ERROR) {
    error = neo_create_error_node(allocator, NULL);
    error->error = token->error;
    token->error = NULL;
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "as")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
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
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return error;
}