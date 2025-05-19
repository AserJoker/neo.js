#include "compiler/export_namespace.h"
#include "compiler/identifier.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>

static void neo_ast_export_namespace_dispose(neo_allocator_t allocator,
                                             neo_ast_export_namespace_t node) {
  neo_allocator_free(allocator, node->identifier);
}

static neo_ast_export_namespace_t
neo_create_ast_export_namespace(neo_allocator_t allocator) {
  neo_ast_export_namespace_t node =
      neo_allocator_alloc2(allocator, neo_ast_export_namespace);
  node->node.type = NEO_NODE_TYPE_EXPORT_NAMESPACE;
  node->identifier = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_export_namespace(neo_allocator_t allocator,
                                             const char *file,
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
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}