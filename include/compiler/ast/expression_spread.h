#ifndef _H_NEO_COMPILER_EXPRESSION_SPREAD__
#define _H_NEO_COMPILER_EXPRESSION_SPREAD__
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_spread_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t value;
} *neo_ast_expression_spread_t;

neo_ast_node_t neo_ast_read_expression_spread(neo_allocator_t allocator,
                                              const wchar_t *file,
                                              neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif