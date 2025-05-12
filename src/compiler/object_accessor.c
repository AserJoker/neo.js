#include "compiler/object_accessor.h"
#include "compiler/expression.h"
#include "compiler/function_argument.h"
#include "compiler/function_body.h"
#include "compiler/identifier.h"
#include "compiler/literal_numeric.h"
#include "compiler/literal_string.h"
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/location.h"
#include "core/position.h"
#include <stdio.h>
static void neo_ast_object_accessor_dispose(neo_allocator_t allocator,
                                            neo_ast_object_accessor_t node) {
  neo_allocator_free(allocator, node->arguments);
  neo_allocator_free(allocator, node->body);
  neo_allocator_free(allocator, node->name);
}

static neo_ast_object_accessor_t
neo_create_ast_object_accessor(neo_allocator_t allocator) {
  neo_ast_object_accessor_t node =
      neo_allocator_alloc2(allocator, neo_ast_object_accessor);
  node->node.type = NEO_NODE_TYPE_OBJECT_ACCESSOR;
  node->body = NULL;
  node->name = NULL;
  node->kind = NEO_ACCESSOR_KIND_GET;
  node->computed = false;
  neo_list_initialize_t initialize = {true};
  node->arguments = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_object_accessor(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_object_accessor_t node = neo_create_ast_object_accessor(allocator);
  neo_token_t token = NULL;
  token = neo_read_identify_token(allocator, file, &current);
  if (!token || (!neo_location_is(token->location, "get") &&
                 !neo_location_is(token->location, "set"))) {
    goto onerror;
  }
  if (neo_location_is(token->location, "get")) {
    node->kind = NEO_ACCESSOR_KIND_GET;
  } else {
    node->kind = NEO_ACCESSOR_KIND_SET;
  }
  neo_allocator_free(allocator, token);
  SKIP_ALL(allocator, file, &current, onerror);
  node->name = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->name) {
    node->name = TRY(neo_ast_read_literal_string(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->name) {
    node->name = TRY(neo_ast_read_literal_numeric(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (*current.offset == '[') {
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
    node->name = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
      goto onerror;
    }
    if (!node->name) {
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
  if (!node->name) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '(') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ')') {
    for (;;) {
      neo_ast_node_t argument =
          TRY(neo_ast_read_function_argument(allocator, file, &current)) {
        goto onerror;
      }
      if (!argument) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      neo_list_push(node->arguments, argument);
      SKIP_ALL(allocator, file, &current, onerror);
      if (*current.offset == ')') {
        break;
      }
      if (*current.offset != ',') {
        goto onerror;
      }
      if (((neo_ast_function_argument_t)argument)->identifier->type ==
          NEO_NODE_TYPE_PATTERN_REST) {
        THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d",
              file, current.line, current.column);
        goto onerror;
      }
      current.offset++;
      current.column++;
      SKIP_ALL(allocator, file, &current, onerror);
    }
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node->body = TRY(neo_ast_read_function_body(allocator, file, &current)) {
    goto onerror;
  }
  if (!node->body) {
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
  neo_allocator_free(allocator, token);
  neo_allocator_free(allocator, node);
  return NULL;
}