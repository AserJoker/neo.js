#include "compiler/statement_empty.h"
#include "core/variable.h"

static void neo_ast_statement_empty_dispose(neo_allocator_t allocator,
                                            neo_ast_statement_empty_t node) {
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t
neo_serialize_ast_statement_empty(neo_allocator_t allocator,
                                  neo_ast_statement_empty_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_STATEMENT_EMPTY"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  return variable;
}
static neo_ast_statement_empty_t
neo_create_empty_statement(neo_allocator_t allocator) {
  neo_ast_statement_empty_t node =
      neo_allocator_alloc2(allocator, neo_ast_statement_empty);
  node->node.type = NEO_NODE_TYPE_STATEMENT_EMPTY;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_statement_empty;
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
    return &node->node;
  }
  return NULL;
}