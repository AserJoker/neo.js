#include "compiler/literal_null.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"

static void neo_ast_literal_null_dispose() {}

static neo_ast_literal_null_t
neo_create_ast_literal_null(neo_allocator_t allocator) {
  neo_ast_literal_null_t node =
      neo_allocator_alloc2(allocator, neo_ast_literal_null);
  node->node.type = NEO_NODE_TYPE_LITERAL_NULL;
  return node;
}

neo_ast_node_t neo_ast_read_literal_null(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_literal_null_t node = NULL;
  neo_token_t token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "null")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_literal_null(allocator);
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