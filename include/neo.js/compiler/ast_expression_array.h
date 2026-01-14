#ifndef _H_NEO_COMPILER_EXPRESSION_ARRAY_
#define _H_NEO_COMPILER_EXPRESSION_ARRAY_
#include "compiler/ast_node.h"
#include "core/list.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_array_t {
  struct _neo_ast_node_t node;
  neo_list_t items;
} *neo_ast_expression_array_t;

neo_ast_node_t neo_ast_read_expression_array(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif