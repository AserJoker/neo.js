#include "compiler/ast/expression_group.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include "core/position.h"
#include <stdio.h>

neo_ast_node_t neo_ast_read_expression_group(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_ast_node_t error = NULL;
  neo_position_t current = *position;
  neo_ast_node_t node = NULL;
  if (*current.offset != '(') {
    return NULL;
  }
  current.offset++;
  current.column++;

  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  node = neo_ast_read_expression(allocator, file, &current);
  if (!node) {
    goto onerror;
  }
  NEO_CHECK_NODE(node, error, onerror);
  error = neo_skip_all(allocator, file, &current);
  if (error) {
    goto onerror;
  }
  if (*current.offset != ')') {
    goto onerror;
  }
  current.offset++;
  current.column++;
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, node);
  return error;
}