#ifndef _H_NOIX_COMPILER_STATEMENT_EMPTY_
#define _H_NOIX_COMPILER_STATEMENT_EMPTY_
#include "compiler/node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _noix_empty_statement_t {
  struct _noix_ast_node_t node;
} *noix_empty_statement_t;
noix_ast_node_t noix_read_empty_statement(noix_allocator_t allocator,
                                          const char *file,
                                          noix_position_t *position);
#ifdef __cplusplus
}
#endif
#endif