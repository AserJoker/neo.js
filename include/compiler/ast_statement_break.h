#ifndef _H_NEO_COMPILER_STATEMENT_BREAK_
#define _H_NEO_COMPILER_STATEMENT_BREAK_
#include "compiler/ast_node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_statement_break_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t label;
} *neo_ast_statement_break_t;

neo_ast_node_t neo_ast_read_statement_break(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif