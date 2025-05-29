#include "compiler/ast/import_namespace.h"
#include "compiler/ast/identifier.h"
#include "compiler/token.h"
#include "core/variable.h"
#include <stdio.h>
static void neo_ast_import_namespace_dispose(neo_allocator_t allocator,
                                             neo_ast_import_namespace_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_import_namespace(neo_allocator_t allocator,
                                   neo_ast_import_namespace_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_IMPORT_NAMESPACE"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  return variable;
}

static neo_ast_import_namespace_t
neo_create_ast_import_namespace(neo_allocator_t allocator) {
  neo_ast_import_namespace_t node =
      neo_allocator_alloc2(allocator, neo_ast_import_namespace);
  node->node.type = NEO_NODE_TYPE_IMPORT_NAMESPACE;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_import_namespace;
  node->identifier = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_import_namespace(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_import_namespace_t node = neo_create_ast_import_namespace(allocator);
  if (*current.offset != '*') {
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "as")) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                           node->identifier, NEO_COMPILE_VARIABLE_CONST);
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  neo_allocator_free(allocator, token);
  return NULL;
}