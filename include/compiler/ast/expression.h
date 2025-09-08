#ifndef _H_NEO_COMPILER_EXPRESSION__
#define _H_NEO_COMPILER_EXPRESSION__
#include "compiler/ast/node.h"
#include "compiler/token.h"
#include "core/allocator.h"
#include "core/position.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_binary_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t left;
  neo_ast_node_t right;
  neo_token_t opt;
} *neo_ast_expression_binary_t;

neo_ast_node_t neo_ast_read_expression_1(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_2(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_3(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_4(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_5(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_6(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_7(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_8(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_9(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_10(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_11(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_12(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_13(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_14(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_15(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_16(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_17(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_18(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);
neo_ast_node_t neo_ast_read_expression_19(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);

#define neo_ast_read_expression neo_ast_read_expression_1
#ifdef __cplusplus
}
#endif
#endif