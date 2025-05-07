#ifndef _H_NOIX_COMPILER_STATEMENT_EMPTY_
#define _H_NOIX_COMPILER_STATEMENT_EMPTY_
#include "compiler/node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _noix_ast_statement_empty_t {
  struct _noix_ast_node_t node;
} *noix_ast_statement_empty_t;
noix_ast_node_t noix_ast_read_statement_empty(noix_allocator_t allocator,
                                              const char *file,
                                              noix_position_t *position);
#ifdef __cplusplus
}
#endif
#endif