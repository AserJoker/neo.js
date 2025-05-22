#include "compiler/variable_declarator.h"
#include "compiler/expression.h"
#include "compiler/identifier.h"
#include "compiler/pattern_array.h"
#include "compiler/pattern_object.h"
#include "core/variable.h"
#include <stdio.h>
static void
neo_ast_variable_declarator_dispose(neo_allocator_t allocator,
                                    neo_ast_variable_declarator_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->initialize);
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t
neo_serialize_ast_variable_declarator(neo_allocator_t allocator,
                                      neo_ast_variable_declarator_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_VARIABLE_DECLARATOR"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "identifier",
                   neo_ast_node_serialize(allocator, node->identifier));
  neo_variable_set(variable, "initialize",
                   neo_ast_node_serialize(allocator, node->initialize));
  return variable;
}
static neo_ast_variable_declarator_t
neo_create_ast_variable_declarator(neo_allocator_t allocator) {
  neo_ast_variable_declarator_t node =
      neo_allocator_alloc2(allocator, neo_ast_variable_declarator);
  node->node.type = NEO_NODE_TYPE_VARIABLE_DECLARATOR;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn)neo_serialize_ast_variable_declarator;
  node->identifier = NULL;
  node->initialize = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_variable_declarator(neo_allocator_t allocator,
                                                const char *file,
                                                neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_variable_declarator_t node =
      neo_create_ast_variable_declarator(allocator);
  node->identifier =
      TRY(neo_ast_read_pattern_object(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_array(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    node->identifier = TRY(neo_ast_read_identifier(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  neo_position_t cur = current;
  SKIP_ALL(allocator, file, &cur, onerror);
  if (*cur.offset == '=') {
    current = cur;
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
    node->initialize =
        TRY(neo_ast_read_expression_2(allocator, file, &current)) {
      goto onerror;
    };
    if (!node->initialize) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}