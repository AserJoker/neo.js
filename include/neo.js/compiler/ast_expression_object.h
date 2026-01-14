#ifndef _H_NEO_COMPILER_EXPRESSION_OBJECT_
#define _H_NEO_COMPILER_EXPRESSION_OBJECT_
#include "compiler/ast_node.h"
#include "core/list.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_object_t {
  struct _neo_ast_node_t node;
  neo_list_t items;
} *neo_ast_expression_object_t;

neo_ast_node_t neo_ast_read_expression_object(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif