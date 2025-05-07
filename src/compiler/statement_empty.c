#include "compiler/statement_empty.h"
static noix_ast_statement_empty_t
noix_create_empty_statement(noix_allocator_t allocator) {
  noix_ast_statement_empty_t node =
      (noix_ast_statement_empty_t)noix_allocator_alloc(
          allocator, sizeof(struct _noix_ast_statement_empty_t), NULL);
  node->node.type = NOIX_NODE_TYPE_STATEMENT_EMPTY;
  return node;
}

noix_ast_node_t noix_ast_read_statement_empty(noix_allocator_t allocator,
                                              const char *file,
                                              noix_position_t *position) {
  if (*position->offset == ';') {
    noix_ast_statement_empty_t node = noix_create_empty_statement(allocator);
    node->node.location.file = file;
    node->node.location.begin = *position;
    node->node.location.end = *position;
    position->column++;
    position->offset++;
    return &node->node;
  }
  return NULL;
}