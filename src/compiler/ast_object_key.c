#include "neo.js/compiler/ast_object_key.h"
#include "neo.js/compiler/ast_expression.h"
#include "neo.js/compiler/ast_identifier.h"
#include "neo.js/compiler/ast_literal_numeric.h"
#include "neo.js/compiler/ast_literal_string.h"
#include "neo.js/compiler/ast_node.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/position.h"
#include <stdio.h>

neo_ast_node_t neo_ast_read_object_key(neo_allocator_t allocator,
                                       const char *file,
                                       neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_node_t node = NULL;
  neo_ast_node_t error = NULL;
  node = neo_ast_read_identifier(allocator, file, &current);
  if (!node) {
    node = neo_ast_read_literal_string(allocator, file, &current);
  }
  if (!node) {
    node = neo_ast_read_literal_numeric(allocator, file, &current);
  }
  if (!node) {
    goto onerror;
  }
  NEO_CHECK_NODE(node, error, onerror);
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, node);
  return error;
}

neo_ast_node_t neo_ast_read_object_computed_key(neo_allocator_t allocator,
                                                const char *file,
                                                neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_node_t error = NULL;
  neo_ast_node_t node = NULL;
  if (*current.offset != '[') {
    goto onerror;
  }
  current.offset++;
  current.column++;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node = neo_ast_read_expression_2(allocator, file, &current);
  if (!node) {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  NEO_CHECK_NODE(node, error, onerror);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != ']') {
    error = neo_create_error_node(
        allocator, "Invalid or unexpected token \n  at _.compile (%s:%d:%d)",
        file, current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  if (!node) {
    goto onerror;
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, node);
  return error;
}