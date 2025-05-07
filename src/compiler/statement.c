#include "compiler/statement.h"
#include "compiler/statement_block.h"
#include "compiler/statement_empty.h"
#include "core/error.h"
neo_ast_node_t neo_ast_read_statement(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position) {
  neo_ast_node_t node = NULL;
  node = TRY(neo_ast_read_statement_empty(allocator, file, position)) {
    goto onerror;
  }
  if (!node) {
    node = TRY(neo_ast_read_statement_block(allocator, file, position)) {
      goto onerror;
    }
  }
  return node;
onerror:
  return NULL;
}
