#include "compiler/declaration_function.h"
#include "compiler/expression_function.h"
#include "compiler/scope.h"
#include "core/variable.h"
#include <stdio.h>
static void
neo_ast_declaration_function_dispose(neo_allocator_t allocator,
                                     neo_ast_declaration_function_t node) {
  neo_allocator_free(allocator, node->declaration);
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t
neo_serialize_ast_declaration_function(neo_allocator_t allocator,
                                       neo_ast_declaration_function_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(variable, "type",
                   neo_create_variable_string(
                       allocator, "NEO_NODE_TYPE_DECLARATION_FUNCTION"));
  neo_variable_set(variable, "declaration",
                   neo_ast_node_serialize(allocator, node->declaration));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  return variable;
}

static neo_ast_declaration_function_t
neo_create_ast_declaration_function(neo_allocator_t allocator) {
  neo_ast_declaration_function_t node =
      neo_allocator_alloc2(allocator, neo_ast_declaration_function);
  node->node.type = NEO_NODE_TYPE_DECLARATION_FUNCTION;
  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn)neo_serialize_ast_declaration_function;
  node->declaration = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_declaration_function(neo_allocator_t allocator,
                                                 const char *file,
                                                 neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_function_t declaration = NULL;
  neo_ast_declaration_function_t node =
      neo_create_ast_declaration_function(allocator);
  declaration = (neo_ast_expression_function_t)neo_ast_read_expression_function(
      allocator, file, &current);
  if (!declaration) {
    goto onerror;
  }
  node->declaration = &declaration->node;
  if (!declaration->name) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  neo_compile_scope_declar(allocator, neo_compile_scope_get_current(),
                           node->declaration, NEO_COMPILE_VARIABLE_FUNCTION);
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}