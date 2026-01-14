#ifndef _H_NEO_COMPILER_EXPRESSION_ARROW_FUNCTION__
#define _H_NEO_COMPILER_EXPRESSION_ARROW_FUNCTION__
#include "compiler/ast_node.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_arrow_function_t {
  struct _neo_ast_node_t node;
  neo_list_t arguments;
  neo_list_t closure;
  neo_ast_node_t body;
  bool async;
} *neo_ast_expression_arrow_function_t;

neo_ast_node_t neo_ast_read_expression_arrow_function(neo_allocator_t allocator,
                                                      const char *file,
                                                      neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif