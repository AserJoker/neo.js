#ifndef _H_NOIX_COMPILER_LITERAL_STRING_
#define _H_NOIX_COMPILER_LITERAL_STRING_
#include "compiler/node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _noix_ast_literal_string_node_t {
  struct _noix_ast_node_t node;
} *noix_ast_literal_string_node_t;

noix_ast_node_t noix_ast_read_literal_string(noix_allocator_t allocator,
                                             const char *file,
                                             noix_position_t *position);
#ifdef __cplusplus
}
#endif
#endif