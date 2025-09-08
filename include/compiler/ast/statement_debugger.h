#ifndef _H_NEO_COMPILER_STATEMENT_DEBUGGER_
#define _H_NEO_COMPILER_STATEMENT_DEBUGGER_
#include "compiler/ast/node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_statement_debugger_t {
  struct _neo_ast_node_t node;
} *neo_ast_statement_debugger_t;

neo_ast_node_t neo_ast_read_statement_debugger(neo_allocator_t allocator,
                                               const char *file,
                                               neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif