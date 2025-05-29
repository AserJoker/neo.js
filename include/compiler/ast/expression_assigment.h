#ifndef _H_NEO_COMPILER_EXPRESSION_ASSIGMENT__
#define _H_NEO_COMPILER_EXPRESSION_ASSIGMENT__
#include "compiler/ast/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_assigment_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t identifier;
  neo_ast_node_t value;
  neo_token_t opt;
} *neo_ast_expression_assigment_t;

neo_ast_node_t neo_ast_read_expression_assigment(neo_allocator_t allocator,
                                                 const char *file,
                                                 neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif