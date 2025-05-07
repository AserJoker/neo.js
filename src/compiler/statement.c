#include "compiler/statement.h"
#include "compiler/statement_block.h"
#include "compiler/statement_empty.h"
#include "core/error.h"
noix_ast_node_t noix_ast_read_statement(noix_allocator_t allocator,
                                        const char *file,
                                        noix_position_t *position) {
  noix_ast_node_t node = NULL;
  node = TRY(noix_ast_read_statement_empty(allocator, file, position)) {
    goto onerror;
  }
  if (!node) {
    node = TRY(noix_ast_read_statement_block(allocator, file, position)) {
      goto onerror;
    }
  }
  return node;
onerror:
  return NULL;
}
