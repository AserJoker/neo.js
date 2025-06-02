#include "compiler/ast/expression_object.h"
#include "compiler/ast/expression_spread.h"
#include "compiler/ast/node.h"
#include "compiler/ast/object_accessor.h"
#include "compiler/ast/object_method.h"
#include "compiler/ast/object_property.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/position.h"
#include "core/variable.h"
#include <stdio.h>

static void
neo_ast_expression_object_dispose(neo_allocator_t allocator,
                                  neo_ast_expression_object_t node) {
  neo_allocator_free(allocator, node->items);
  neo_allocator_free(allocator, node->node.scope);
}
static neo_variable_t
neo_serialize_ast_expression_object(neo_allocator_t allocator,
                                    neo_ast_expression_object_t node) {
  neo_variable_t variable = neo_create_variable_dict(allocator, NULL, NULL);
  neo_variable_set(
      variable, "type",
      neo_create_variable_string(allocator, "NEO_NODE_TYPE_EXPRESSION_OBJECT"));
  neo_variable_set(variable, "location",
                   neo_ast_node_location_serialize(allocator, &node->node));
  neo_variable_set(variable, "scope",
                   neo_serialize_scope(allocator, node->node.scope));
  neo_variable_set(variable, "items",
                   neo_ast_node_list_serialize(allocator, node->items));
  return variable;
}

static neo_ast_expression_object_t
neo_create_ast_expression_object(neo_allocator_t allocator) {
  neo_ast_expression_object_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_object);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_OBJECT;

  node->node.scope = NULL;
  node->node.serialize =
      (neo_serialize_fn_t)neo_serialize_ast_expression_object;
  neo_list_initialize_t initialize = {true};
  node->items = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_expression_object(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_object_t node =
      neo_create_ast_expression_object(allocator);
  if (*current.offset != '{') {
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '}') {
    for (;;) {
      neo_ast_node_t item = NULL;
      if (!item) {
        item = TRY(neo_ast_read_object_accessor(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        item = TRY(neo_ast_read_object_method(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        item = TRY(neo_ast_read_expression_spread(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        item = TRY(neo_ast_read_object_property(allocator, file, &current)) {
          goto onerror;
        };
      }
      if (!item) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      neo_list_push(node->items, item);
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset == ',') {
        current.offset++;
        current.column++;
        SKIP_ALL(allocator, file, &current, onerror);
      } else if (*current.offset == '}') {
        break;
      } else {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
    }
  }
  current.offset++;
  current.column++;
  node->node.location.file = file;
  node->node.location.begin = *position;
  node->node.location.end = current;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}