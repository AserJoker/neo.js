#include "compiler/statement_block.h"
#include "compiler/node.h"
#include "compiler/statement.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/position.h"
#include <stdio.h>

static void neo_ast_statement_block_dispose(neo_allocator_t allocator,
                                            neo_ast_statement_block_t node) {
  neo_allocator_free(allocator, node->body);
}

static neo_ast_statement_block_t
neo_create_statement_block(neo_allocator_t allocator) {
  neo_ast_statement_block_t node =
      (neo_ast_statement_block_t)neo_allocator_alloc(
          allocator, sizeof(struct _neo_ast_statement_block_t), NULL);
  node->node.type = NEO_NODE_TYPE_STATEMENT_BLOCK;
  neo_list_initialize_t initialize = {};
  initialize.auto_free = true;
  node->body = neo_create_list(allocator, &initialize);
  return node;
}

neo_ast_node_t neo_ast_read_statement_block(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position) {
  neo_position_t current = *position;
  if (*current.offset != '{') {
    return NULL;
  }
  neo_ast_statement_block_t node = neo_create_statement_block(allocator);
  current.offset++;
  current.column++;
  SKIP_ALL(allocator, file, &current, onerror);
  while (true) {
    neo_ast_node_t statement =
        TRY(neo_ast_read_statement(allocator, file, &current)) {
      goto onerror;
    };
    if (!statement) {
      break;
    }
    neo_list_push(node->body, statement);
    SKIP_ALL(allocator, file, &current, onerror);
    if (*current.offset == ';') {
      current.offset++;
      current.column++;
      SKIP_ALL(allocator, file, &current, onerror);
    }
  }
  SKIP_ALL(allocator, file, &current, onerror);
  if (*current.offset != '}') {
    THROW("SyntaxError", "Invalid or unexpected token \n  at %s:%d:%d", file,
          current.line, current.column);
    goto onerror;
  }
  current.offset++;
  current.column++;
  node->node.location.begin = *position;
  node->node.location.end = current;
  node->node.location.file = file;
  *position = current;
  return &node->node;
onerror:
  if (node) {
    neo_allocator_free(allocator, node);
  }
  return NULL;
}