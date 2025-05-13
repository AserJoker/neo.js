#include "compiler/expression_class.h"
#include "compiler/class_accessor.h"
#include "compiler/class_method.h"
#include "compiler/class_property.h"
#include "compiler/expression.h"
#include "compiler/identifier.h"
#include "compiler/node.h"
#include "compiler/static_block.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>

static void neo_ast_expression_class_dispose(neo_allocator_t allocator,
                                             neo_ast_expression_class_t node) {
  neo_allocator_free(allocator, node->name);
  neo_allocator_free(allocator, node->extends);
  neo_allocator_free(allocator, node->items);
  neo_allocator_free(allocator, node->decorators);
}

static neo_ast_expression_class_t
neo_create_ast_expression_class(neo_allocator_t allocator) {
  neo_ast_expression_class_t node =
      neo_allocator_alloc2(allocator, neo_ast_expression_class);
  node->node.type = NEO_NODE_TYPE_EXPRESSION_CLASS;
  node->name = NULL;
  node->extends = NULL;
  neo_list_initialize_t initialize = {true};
  node->items = neo_create_list(allocator, &initialize);
  node->decorators = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_expression_class(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_expression_class_t node = NULL;
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || !neo_location_is(token->location, "class")) {
    goto onerror;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node = neo_create_ast_expression_class(allocator);
  node->name = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  token = neo_read_identify_token(allocator, file, &current);
  SKIP_ALL(allocator, file, &current, onerror);
  if (token && neo_location_is(token->location, "extends")) {
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset == '{') {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    node->extends = neo_ast_read_expression_2(allocator, file, &current);
    SKIP_ALL(allocator, file, &current, onerror);
  }
  neo_allocator_free(allocator, token);
  if (*current.offset != '{') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  if (*current.offset != '}') {
    for (;;) {
      neo_ast_node_t item = NULL;
      if (!item) {
        item = TRY(neo_ast_read_static_block(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        item = TRY(neo_ast_read_class_accessor(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        item = TRY(neo_ast_read_class_method(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        item = TRY(neo_ast_read_class_property(allocator, file, &current)) {
          goto onerror;
        }
      }
      if (!item) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      neo_list_push(node->items, item);
      uint32_t line = current.line;
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset == ';') {
        while (*current.offset == ';') {
          current.offset++;
          current.column++;
          SKIP_ALL(allocator, file, &current, onerror);
        }
      } else if (*current.offset == '}') {
        break;
      } else if (current.line == line &
                 item->type == NEO_NODE_TYPE_CLASS_PROPERTY) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
    }
  }
  current.offset++;
  current.column++;
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}