#include "compiler/directive.h"
#include "compiler/literal_string.h"
#include "compiler/node.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include "core/variable.h"

static void neo_ast_directive_dispose(neo_allocator_t allocator,
                                      neo_ast_directive_t node) {
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t neo_serialize_ast_directive(neo_allocator_t allocator,
                                                  neo_ast_directive_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_DIRECTIVE"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_directive_t neo_create_ast_directive(neo_allocator_t allocator) {
  neo_ast_directive_t node = neo_allocator_alloc2(allocator, neo_ast_directive);
  node->node.type = NEO_NODE_TYPE_DIRECTIVE;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_directive;
  return node;
}

neo_ast_node_t neo_ast_read_directive(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_directive_t node = NULL;
  neo_ast_node_t token =
      TRY(neo_ast_read_literal_string(allocator, file, &current)) {
    goto onerror;
  };
  if (!token) {
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
  node = neo_create_ast_directive(allocator);
  node->node.location.begin = *position;
  node->node.location.end = backup;
  node->node.location.file = file;
  *position = backup;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}