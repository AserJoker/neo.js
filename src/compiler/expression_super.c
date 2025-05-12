#include "compiler/expression_super.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/location.h"
#include "core/position.h"

static void neo_ast_expression_super_dispose() {}

static neo_ast_expression_super_t
neo_create_ast_expression_super(neo_allocator_t allocator) {
  neo_ast_expression_super_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_super);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_SUPER;
  return node;
}

neo_ast_node_t neo_ast_read_expression_super(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_super_t node = NULL;
  neo_token_t token = TRY(neo_read_identify_token(allocator, file, &current)) {
    goto onerror;
  }
  if (!token || !neo_location_is(token->location, "super")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  node = neo_create_ast_expression_super(allocator);
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