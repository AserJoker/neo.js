#include "compiler/function_argument.h"
#include "compiler/expression.h"
#include "compiler/identifier.h"
#include "compiler/node.h"
#include "compiler/pattern_array.h"
#include "compiler/pattern_object.h"
#include "compiler/pattern_rest.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"

static void
neo_ast_function_argument_dispose(neo_allocator_t allocator,
                                  neo_ast_function_argument_t node) {
  neo_allocator_free(allocator, node->identifier);
  neo_allocator_free(allocator, node->value);
}

static neo_ast_function_argument_t
neo_create_ast_function_argument(neo_allocator_t allocator) {
  neo_ast_function_argument_t node =
      neo_allocator_alloc2(allocator, neo_ast_function_argument);
  node->identifier = NULL;
  node->value = NULL;
  node->node.type = NEO_NODE_TYPE_FUNCTION_ARGUMENT;
  return node;
}

neo_ast_node_t neo_ast_read_function_argument(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_function_argument_t node = NULL;
  neo_token_t token = NULL;
  node = neo_create_ast_function_argument(allocator);
  node->identifier = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  };
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_array(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_object(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    node->identifier =
        TRY(neo_ast_read_pattern_rest(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node->identifier) {
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset == '=') {
    current.offset++;
    current.column++;
    SKIP_ALL(allocator, file, &current, onerror);
    node->value = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
      goto onerror;
    }
    if (!node->value) {
      goto onerror;
    }
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