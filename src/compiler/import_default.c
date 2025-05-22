#include "compiler/import_default.h"
#include "compiler/identifier.h"
#include "compiler/scope.h"
#include "core/variable.h"

static void neo_ast_import_default_dispose(neo_allocator_t allocator,
                                           neo_ast_import_default_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_import_default(neo_allocator_t allocator,
                                 neo_ast_import_default_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_IMPORT_DEFAULT"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  return variable;
}

static neo_ast_import_default_t
neo_create_ast_import_default(neo_allocator_t allocator) {
  neo_ast_import_default_t node =
      neo_allocator_alloc2(allocator, neo_ast_import_default);
  node->node.type = NEO_NODE_TYPE_IMPORT_DEFAULT;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_import_default;
  node->identifier = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_import_default(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_import_default_t node = neo_create_ast_import_default(allocator);
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  neo_compile_scope_declar(allocator, neo_complile_scope_get_current(),
                           node->identifier, NEO_COMPILE_VARIABLE_CONST);
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}