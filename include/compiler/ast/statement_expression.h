#ifndef _H_NEO_COMPILER_STATEMENT_EXPRESSION_
#define _H_NEO_COMPILER_STATEMENT_EXPRESSION_
#include "compiler/ast/node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_statement_expression_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t expression;
} *neo_ast_statement_expression_t;

neo_ast_node_t neo_ast_read_statement_expression(neo_allocator_t allocator,
                                                 const wchar_t *file,
                                                 neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif