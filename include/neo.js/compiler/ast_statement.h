#ifndef _H_NEO_COMPILER_STATEMENT_
#define _H_NEO_COMPILER_STATEMENT_
#include "neo.js/compiler/ast_node.h"
#ifdef __cplusplus
extern "C" {
#endif

neo_ast_node_t neo_ast_read_statement(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif