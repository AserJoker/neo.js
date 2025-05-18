#include "compiler/import_default_specifier.h"
#include "compiler/identifier.h"

static void neo_ast_import_default_specifier_dispose(
    neo_allocator_t allocator, neo_ast_import_default_specifier_t node) {
  neo_allocator_free(allocator, node->identifier);
}

static neo_ast_import_default_specifier_t
neo_create_ast_import_default_specifier(neo_allocator_t allocator) {
  neo_ast_import_default_specifier_t node =
      neo_allocator_alloc2(allocator, neo_ast_import_default_specifier);
  node->node.type = NEO_NODE_TYPE_IMPORT_DEFAULT_SPECIFIER;
  node->identifier = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_import_default_specifier(neo_allocator_t allocator,
                                                     const char *file,
                                                     neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_import_default_specifier_t node =
      neo_create_ast_import_default_specifier(allocator);
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