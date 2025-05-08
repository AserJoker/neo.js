#ifndef _H_NEO_COMPILER_EXPRESSION__
#define _H_NEO_COMPILER_EXPRESSION__
#include "compiler/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/position.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_binary_expression_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t left;
  neo_ast_node_t right;
  neo_token_t opt;
} *neo_ast_binary_expression_t;

neo_ast_node_t neo_ast_read_expression(neo_allocator_t allocator,
                                       const char *file,
                                       neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif