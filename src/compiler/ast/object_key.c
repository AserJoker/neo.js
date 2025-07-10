#include "compiler/ast/object_key.h"
#include "compiler/ast/expression.h"
#include "compiler/ast/identifier.h"
#include "compiler/ast/literal_numeric.h"
#include "compiler/ast/literal_string.h"
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/position.h"
#include <stdio.h>

neo_ast_node_t neo_ast_read_object_key(neo_allocator_t allocator,
                                       const wchar_t *file,
                                       neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_node_t node = NULL;
  node = TRY(neo_ast_read_identifier(allocator, file, &current)) {
    goto onerror;
  };
  if (!node) {
    node = TRY(neo_ast_read_literal_string(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node) {
    node = TRY(neo_ast_read_literal_numeric(allocator, file, &current)) {
      goto onerror;
    }
  }
  if (!node) {
    goto onerror;
  }
  *position = current;
  return node;
onerror:
  neo_allocator_free(allocator, node);
  return NULL;
}

neo_ast_node_t neo_ast_read_object_computed_key(neo_allocator_t allocator,
                                                const wchar_t *file,
                                                neo_position_t *position) {
  neo_position_t current = *position;
  neo_ast_node_t node = NULL;
  if (*current.offset != '[') {
    goto onerror;
  }
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  node = TRY(neo_ast_read_expression_2(allocator, file, &current)) {
    goto onerror;
  }
  if (!node) {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
    goto onerror;
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != ']') {
    THROW("Invalid or unexpected token \n  at _.compile (%ls:%d:%d)", file,
          current.line, current.column);
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
  return NULL;
}