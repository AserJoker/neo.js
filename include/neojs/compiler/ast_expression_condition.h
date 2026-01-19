#ifndef _H_NEO_COMPILER_EXPRESSION_CONDITION__
#define _H_NEO_COMPILER_EXPRESSION_CONDITION__
#include "neojs/compiler/ast_node.h"
#include "neojs/core/allocator.h"
#include "neojs/core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_condition_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t condition;
  neo_ast_node_t alternate;
  neo_ast_node_t consequent;
} *neo_ast_expression_condition_t;

neo_ast_node_t neo_ast_read_expression_condition(neo_allocator_t allocator,
                                                 const char *file,
                                                 neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif