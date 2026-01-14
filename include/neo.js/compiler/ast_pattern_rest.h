#ifndef _H_NEO_COMPILER_PATTERN_REST_
#define _H_NEO_COMPILER_PATTERN_REST_
#include "compiler/ast_node.h"
#include "core/allocator.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_pattern_rest_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t identifier;
} *neo_ast_pattern_rest_t;

neo_ast_node_t neo_ast_read_pattern_rest(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif