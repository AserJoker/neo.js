#include "compiler/statement_empty.h"
static neo_ast_statement_empty_t
neo_create_empty_statement(neo_allocator_t allocator) {
  neo_ast_statement_empty_t node =
      (neo_ast_statement_empty_t)neo_allocator_alloc(
          allocator, sizeof(struct _neo_ast_statement_empty_t), NULL);
  node->node.type = NEO_NODE_TYPE_STATEMENT_EMPTY;
  return node;
}

neo_ast_node_t neo_ast_read_statement_empty(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  if (*position->offset == ';') {
    neo_ast_statement_empty_t node = neo_create_empty_statement(allocator);
    node->node.location.file = file;
    node->node.location.begin = *position;
    node->node.location.end = *position;
    position->column++;
    position->offset++;
    return &node->node;
  }
  return NULL;
}