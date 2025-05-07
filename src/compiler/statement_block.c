#include "compiler/statement_block.h"
#include "compiler/node.h"
#include "compiler/statement.h"
#include "core/allocator.h"
#include "core/error.h"
#include "core/list.h"
#include "core/position.h"
#include <stdio.h>

static void noix_ast_statement_block_dispose(noix_allocator_t allocator,
                                             noix_ast_statement_block_t node) {
  noix_allocator_free(allocator, node->body);
}

static noix_ast_statement_block_t
noix_create_statement_block(noix_allocator_t allocator) {
  noix_ast_statement_block_t node =
      (noix_ast_statement_block_t)noix_allocator_alloc(
          allocator, sizeof(struct _noix_ast_statement_block_t), NULL);
  node->node.type = NOIX_NODE_TYPE_STATEMENT_BLOCK;
  noix_list_initialize_t initialize = {};
  initialize.auto_free = true;
  node->body = noix_create_list(allocator, &initialize);
  return node;
}

noix_ast_node_t noix_ast_read_statement_block(noix_allocator_t allocator,
                                              const char *file,
                                              noix_position_t *position) {
  noix_position_t current = *position;
  if (*current.offset != '{') {
    return NULL;
  }
  noix_ast_statement_block_t node = noix_create_statement_block(allocator);
  current.offset++;
  current.column++;
  SKIP_ALL();
  while (true) {
    noix_ast_node_t statement =
        TRY(noix_ast_read_statement(allocator, file, &current)) {
      goto onerror;
    };
    if (!statement) {
      break;
    }
    noix_list_push(node->body, statement);
    SKIP_ALL();
  }
  SKIP_ALL();
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
    noix_allocator_free(allocator, node);
  }
  return NULL;
}