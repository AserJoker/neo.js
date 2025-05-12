#include "compiler/literal_numeric.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"

static void neo_ast_literal_numeric_dispose() {}

static neo_ast_literal_numeric_t
neo_create_ast_literal_numeric(neo_allocator_t allocator) {
  neo_ast_literal_numeric_t node =
      neo_allocator_alloc2(allocator, neo_ast_literal_numeric);
  node->node.type = NEO_NODE_TYPE_LITERAL_NUMERIC;
  return node;
}

neo_ast_node_t neo_ast_read_literal_numeric(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_literal_numeric_t node = NULL;
  neo_token_t token = TRY(neo_read_number_token(allocator, file, &current)) {
    goto onerror;
  }
  if (!token) {
    return NULL;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_literal_numeric(allocator);
  neo_position_t cur = current;
  token = TRY(neo_read_identify_token(allocator, file, &cur)) { goto onerror; }
  if (token && neo_location_is(token->location, "n")) {
    node->node.type = NEO_NODE_TYPE_LITERAL_BIGINT;
    current = cur;
  }
  neo_allocator_free(allocator, token);
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