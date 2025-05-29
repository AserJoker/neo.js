#include "compiler/import_specifier.h"
#include "compiler/identifier.h"
#include "compiler/literal_string.h"
#include "compiler/token.h"
#include "core/error.h"
#include "core/variable.h"
#include <stdio.h>

static void neo_ast_import_specifier_dispose(neo_allocator_t allocator,
                                             neo_ast_import_specifier_t node) {
  neo_allocator_free(allocator, node->alias);
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->node.scope);
}

static neo_variable_t
neo_serialize_ast_import_specifier(neo_allocator_t allocator,
                                   neo_ast_import_specifier_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_IMPORT_SPECIFIER"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, "alias",
                   neo_ast_node_serialize(allocator, node->alias));
  return variable;
}

static neo_ast_import_specifier_t
neo_create_ast_import_specifier(neo_allocator_t allocator) {
  neo_ast_import_specifier_t node =
      neo_allocator_alloc2(allocator, neo_ast_import_specifier);
  node->node.type = NEO_NODE_TYPE_IMPORT_SPECIFIER;

  node->node.scope = NULL;
  node->node.serialize = (neo_serialize_fn)neo_serialize_ast_import_specifier;
  node->alias = NULL;
  node->identifier = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_import_specifier(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_token_t token = NULL;
  neo_ast_import_specifier_t node = neo_create_ast_import_specifier(allocator);
  node->identifier = neo_ast_read_identifier(allocator, file, &current);
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_literal_string(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    goto onerror;
  }
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  token = neo_read_identify_token(allocator, file, &cur);
  if (token && neo_location_is(token->location, "as")) {
    neo_allocator_free(allocator, token);
    current = cur;
    SKIP_ALL(allocator, file, &current, onerror);
    node->alias = neo_ast_read_identifier(allocator, file, &current);
    if (!node->alias) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
  }
  neo_allocator_free(allocator, token);
  if (node->alias) {
    neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                             node->alias, NEO_COMPILE_VARIABLE_CONST);
  } else {
    neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                             node->identifier, NEO_COMPILE_VARIABLE_CONST);
  }
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