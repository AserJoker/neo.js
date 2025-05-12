#ifndef _H_NEO_COMPILER_EXPRESSION_NEW__
#define _H_NEO_COMPILER_EXPRESSION_NEW__
#include "compiler/node.h"
#include "core/list.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_new_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t callee;
  neo_list_t arguments;
} *neo_ast_expression_new_t;

neo_ast_node_t neo_ast_read_expression_new(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif