#include "compiler/expression_array.h"
#include "compiler/expression.h"
#include "compiler/expression_spread.h"
#include "compiler/node.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/position.h"
#include <stdio.h>
static void neo_ast_expression_array_dispose(neo_allocator_t allocator,
                                             neo_ast_expression_array_t node) {
  neo_allocator_free(allocator, node->items);
}
static neo_ast_expression_array_t
neo_create_ast_expression_array(neo_allocator_t allocator) {
  neo_ast_expression_array_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_array);
  neo_list_initialize_t initialize = {true};
  node->node.type = NEO_NODE_TYPE_EXPRESSION_ARRAY;
  node->items = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_expression_array(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_array_t node = NULL;
  if (*current.offset != '[') {
    return NULL;
  }
  current.offset++;
  current.column++;
  node = neo_create_ast_expression_array(allocator);
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ']') {
    for (;;) {
      neo_ast_node_t item =
          TRY(neo_ast_read_expression_2(allocator, file, &current)) {
        goto onerror;
      };
      if (!item) {
        item = TRY(neo_ast_read_expression_spread(allocator, file, &current)) {
          goto onerror;
        }
      }
      neo_list_push(node->items, item);
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset == ',') {
        current.offset++;
        current.column++;
        SKIP_ALL(allocator, file, &current, onerror);
      } else if (*current.offset == ']') {
        break;
      } else {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
    }
  }
  current.column++;
  current.offset++;
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}