#include "compiler/import_default.h"
#include "compiler/identifier.h"

static void neo_ast_import_default_dispose(neo_allocator_t allocator,
                                           neo_ast_import_default_t node) {
  neo_allocator_free(allocator, node->identifier);
}

static neo_ast_import_default_t
neo_create_ast_import_default(neo_allocator_t allocator) {
  neo_ast_import_default_t node =
      neo_allocator_alloc2(allocator, neo_ast_import_default);
  node->node.type = NEO_NODE_TYPE_IMPORT_DEFAULT;
  node->identifier = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_import_default(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_import_default_t node = neo_create_ast_import_default(allocator);
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
    goto onerror;
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