#include "compiler/literal_string.h"
#include "compiler/token.h"
#include "core/error.h"
static noix_ast_literal_string_node_t
noix_create_string_litreral_node(noix_allocator_t allocator) {
  noix_ast_literal_string_node_t node =
      (noix_ast_literal_string_node_t)noix_allocator_alloc(
          allocator, sizeof(struct _noix_ast_literal_string_node_t), NULL);
  node->node.type = NOIX_NODE_TYPE_LITERAL_STRING;
  return node;
}

noix_ast_node_t noix_ast_read_literal_string(noix_allocator_t allocator,
                                             const char *file,
                                             noix_position_t *position) {
  noix_position_t current = *position;
  noix_ast_literal_string_node_t node = NULL;
  noix_token_t token = TRY(noix_read_string_token(allocator, file, &current)) {
    goto onerror;
  };
  if (!token) {
    return NULL;
  }
  noix_allocator_free(allocator, token);
  node = noix_create_string_litreral_node(allocator);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  if (node) {
    noix_allocator_free(allocator, node);
  }
  return NULL;
}