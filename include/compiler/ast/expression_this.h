#ifndef _H_NEO_COMPILER_EXPRESSION_THIS_
#define _H_NEO_COMPILER_EXPRESSION_THIS_
#include "compiler/ast/node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_this_t {
  struct _neo_ast_node_t node;
} *neo_ast_expression_this_t;

neo_ast_node_t neo_ast_read_expression_this(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif