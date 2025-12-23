#ifndef _H_NEO_COMPILER_LITERAL_UNDEFINED_
#define _H_NEO_COMPILER_LITERAL_UNDEFINED_
#include "compiler/ast_node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_literal_undefined_t {
  struct _neo_ast_node_t node;
} *neo_ast_literal_undefined_t;

neo_ast_node_t neo_ast_read_literal_undefined(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif