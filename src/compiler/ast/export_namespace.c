#include "compiler/ast/export_namespace.h"
#include "compiler/ast/identifier.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/location.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_export_namespace_dispose(neo_allocator_t allocator,
                                             neo_ast_export_namespace_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_export_namespace(neo_allocator_t allocator,
                                   neo_ast_export_namespace_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_EXPORT_NAMESPACE"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  return variable;
}

static neo_ast_export_namespace_t
neo_create_ast_export_namespace(neo_allocator_t allocator) {
  neo_ast_export_namespace_t node =
      neo_allocator_alloc2(allocator, neo_ast_export_namespace);
  node->node.type = NEO_NODE_TYPE_EXPORT_NAMESPACE;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_export_namespace;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->identifier = NULL;
  node->node.write = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_export_namespace(neo_allocator_t allocator,
                                             const wchar_t *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_export_namespace_t node = neo_create_ast_export_namespace(allocator);
  neo_token_t token = NULL;
  if (*current.offset != '*') {
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "as")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
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
  return NULL;
}