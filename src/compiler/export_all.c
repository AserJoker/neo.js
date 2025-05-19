#include "compiler/export_all.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>

static void neo_ast_export_all_dispose(neo_allocator_t allocator,
                                       neo_ast_export_all_t node) {}

static neo_ast_export_all_t
neo_create_ast_export_all(neo_allocator_t allocator) {
  neo_ast_export_all_t node =
      neo_allocator_alloc2(allocator, neo_ast_export_all);
  node->node.type = NEO_NODE_TYPE_EXPORT_ALL;
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