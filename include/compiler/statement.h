#ifndef _H_NOIX_COMPILER_STATEMENT_
#define _H_NOIX_COMPILER_STATEMENT_
#include "compiler/node.h"
#ifdef __cplusplus
extern "C" {
#endif

noix_ast_node_t noix_ast_read_statement(noix_allocator_t allocator,
                                        const char *file,
                                        noix_position_t *position);
#ifdef __cplusplus
}
#endif
#endif