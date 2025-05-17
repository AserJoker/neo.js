#include "compiler/declaration_class.h"
#include "compiler/expression_class.h"
#include <stdio.h>
static void
neo_ast_declaration_class_dispose(neo_allocator_t allocator,
                                  neo_ast_declaration_class_t node) {
  neo_allocator_free(allocator, node->declaration);
}

static neo_ast_declaration_class_t
neo_create_ast_declaration_class(neo_allocator_t allocator) {
  neo_ast_declaration_class_t node =
      neo_allocator_alloc2(allocator, neo_ast_declaration_class);
  node->node.type = NEO_NODE_TYPE_DECLARATION_CLASS;
  node->declaration = NULL;
  return node;
}

neo_ast_node_t neo_ast_read_declaration_class(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_class_t declaration = NULL;
  neo_ast_declaration_class_t node =
      neo_create_ast_declaration_class(allocator);
  declaration = (neo_ast_expression_class_t)neo_ast_read_expression_class(
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
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}