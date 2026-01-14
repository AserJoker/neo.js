#ifndef _H_NEO_COMPILER_EXPRESSION_YIELD__
#define _H_NEO_COMPILER_EXPRESSION_YIELD__
#include "neo.js/compiler/ast_node.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_yield_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t value;
  bool degelate;
} *neo_ast_expression_yield_t;

neo_ast_node_t neo_ast_read_expression_yield(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif