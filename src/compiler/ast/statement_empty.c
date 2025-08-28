#include "compiler/ast/statement_empty.h"
#include "compiler/ast/node.h"
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
      variable, L"type",
      neo_create_variable_string(allocator, L"NEO_NODE_TYPE_STATEMENT_EMPTY"));
  neo_variable_set(variable, L"location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, L"scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}
static void neo_ast_statement_empty_write(neo_allocator_t allocator,
                                          neo_write_context_t ctx,
                                          neo_ast_statement_empty_t self) {}
static neo_ast_statement_empty_t
neo_create_empty_statement(neo_allocator_t allocator) {
  neo_ast_statement_empty_t node =
      neo_allocator_alloc(allocator, sizeof(struct _neo_ast_statement_empty_t),
                          neo_ast_statement_empty_dispose);
  node->node.type = NEO_NODE_TYPE_STATEMENT_EMPTY;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn_t)neo_serialize_ast_statement_empty;
  node->node.resolve_closure = neo_ast_node_resolve_closure;
  node->node.write = (neo_write_fn_t)neo_ast_statement_empty_write;
  return node;
}

neo_ast_node_t neo_ast_read_statement_empty(neo_allocator_t allocator,
                                            const wchar_t *file,
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