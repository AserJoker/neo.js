#ifndef _H_NEO_COMPILER_EXPRESSION_CLASS__
#define _H_NEO_COMPILER_EXPRESSION_CLASS__
#include "compiler/node.h"
#include "core/list.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_class_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t name;
  neo_ast_node_t extends;
  neo_list_t items;
  neo_list_t decorators;
} *neo_ast_expression_class_t;

neo_ast_node_t neo_ast_read_expression_class(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif