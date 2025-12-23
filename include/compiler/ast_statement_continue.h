#ifndef _H_NEO_COMPILER_STATEMENT_CONTINUE_
#define _H_NEO_COMPILER_STATEMENT_CONTINUE_
#include "compiler/ast_node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_statement_continue_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t label;
} *neo_ast_statement_continue_t;

neo_ast_node_t neo_ast_read_statement_continue(neo_allocator_t allocator,
                                               const char *file,
                                               neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif