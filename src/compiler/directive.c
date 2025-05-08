#include "compiler/directive.h"
#include "compiler/expression.h"
#include "compiler/node.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"

static void neo_ast_directive_dispose() {}

static neo_ast_directive_t neo_create_ast_directive(neo_allocator_t allocator) {
  neo_ast_directive_t node = neo_allocator_alloc2(allocator, neo_ast_directive);
  node->node.type = NEO_NODE_TYPE_DIRECTIVE;
  return node;
}

neo_ast_node_t neo_ast_read_directive(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_node_t token =
      TRY(neo_ast_read_expression(allocator, file, &current)) {
    goto onerror;
  };
  if (!token || token->type != NEO_NODE_TYPE_LITERAL_STRING) {
    neo_allocator_free(allocator, token);
    return NULL;
  }
  neo_allocator_free(allocator, token);
  neo_position_t backup = current;
  uint32_t line = current.line;
  SKIP_ALL(allocator, file, &current, onerror);
  if (current.line == line) {
    if (*current.offset && *current.offset != ';') {
      return NULL;
    }
  }
  neo_ast_directive_t node = neo_create_ast_directive(allocator);
  node->node.location.begin = *position;
  node->node.location.end = backup;
  node->node.location.file = file;
  *position = backup;
  return &node->node;
onerror:
  if (node) {
    neo_allocator_free(allocator, node);
  }
  return NULL;
}