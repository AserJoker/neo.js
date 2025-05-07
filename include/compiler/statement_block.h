#ifndef _H_NOIX_COMPILER_STATEMENT_BLOCK_
#define _H_NOIX_COMPILER_STATEMENT_BLOCK_
#include "compiler/node.h"
#include "core/list.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _noix_ast_statement_block_t {
  struct _noix_ast_node_t node;
  noix_list_t body;
} *noix_ast_statement_block_t;
noix_ast_node_t noix_ast_read_statement_block(noix_allocator_t allocator,
                                              const char *file,
                                              noix_position_t *position);
#ifdef __cplusplus
}
#endif
#endif