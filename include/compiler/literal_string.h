#ifndef _H_NEO_COMPILER_LITERAL_STRING_
#define _H_NEO_COMPILER_LITERAL_STRING_
#include "compiler/node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_literal_string_t {
  struct _neo_ast_node_t node;
} *neo_ast_literal_string_t;

neo_ast_node_t neo_ast_read_literal_string(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif