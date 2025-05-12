#include "compiler/object_property.h"
#include "compiler/expression.h"
#include "compiler/identifier.h"
#include "compiler/literal_numeric.h"
#include "compiler/literal_string.h"
#include "compiler/node.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include <stdio.h>

static void neo_ast_object_property_dispose(neo_allocator_t allocator,
                                            neo_ast_object_property_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->value);
}

static neo_ast_object_property_t
neo_create_ast_object_property(neo_allocator_t allocator) {
  neo_ast_object_property_t node =
      neo_allocator_alloc2(allocator, neo_ast_object_property);
  node->identifier = NULL;
  node->value = NULL;
  node->node.type = NEO_NODE_TYPE_OBJECT_PROPERTY;
  node->computed = false;
  return node;
}

neo_ast_node_t neo_ast_read_object_property(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_object_property_t node = neo_create_ast_object_property(allocator);
  node->identifier = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_literal_string(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_literal_numeric(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (*current.offset == '[') {
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
    node->identifier =
        TRY(neo_ast_read_expression_2(allocator, file, &current)) {
      goto onerror;
    }
    if (!node->identifier) {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset != ']') {
      THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
            current.line, current.column);
      goto onerror;
    }
    current.offset++;
    current.column++;
    node->computed = true;
  }
  if (!node->identifier) {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ':') {
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
  };
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}