#ifndef _H_NEO_COMPILER_EXPRESSION_MEMBER__
#define _H_NEO_COMPILER_EXPRESSION_MEMBER__
#include "neojs/compiler/ast_node.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_expression_member_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t field;
  neo_ast_node_t host;
} *neo_ast_expression_member_t;

neo_ast_node_t neo_ast_read_expression_member(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif