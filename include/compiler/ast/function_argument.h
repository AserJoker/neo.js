#ifndef _H_NEO_COMPILER_FUNCTION_ARGUMENT__
#define _H_NEO_COMPILER_FUNCTION_ARGUMENT__
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_function_argument_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t identifier;
  neo_ast_node_t value;
} *neo_ast_function_argument_t;

neo_ast_node_t neo_ast_read_function_argument(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif