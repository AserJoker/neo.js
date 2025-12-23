#include "compiler/ast_export_all.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/any.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>

static void neo_ast_export_all_dispose(neo_allocator_t allocator,
                                       neo_ast_export_all_t node) {
  neo_allocator_free(allocator, node->node.scope);
}

static neo_any_t neo_serialize_ast_export_all(neo_allocator_t allocator,
                                              neo_ast_export_all_t node) {
  neo_any_t variable = neo_create_any_dict(allocator, NULL, NULL);
  neo_any_set(variable, "type",
              neo_create_any_string(allocator, "NEO_NODE_TYPE_EXPORT_A"));
  neo_any_set(variable, "location",
              neo_ast_node_location_serialize(allocator, &node->node));
  neo_any_set(variable, "scope",
              neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_export_all_t
neo_create_ast_export_all(neo_allocator_t allocator) {
  neo_ast_export_all_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_export_all_t),
                          neo_ast_export_all_dispose);
  node->node.type = NEO_NODE_TYPE_EXPORT_ALL;
  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_export_all;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_export_all(neo_allocator_t allocator,
                                       const char *file,
                                       neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_export_all_t node = neo_create_ast_export_all(allocator);
  neo_token_t token = NULL;
  if (*current.offset != '*') {
    goto onerror;
  }
  current.offset++;
  current.column++;
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