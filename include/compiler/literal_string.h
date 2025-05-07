#ifndef _H_NOIX_COMPILER_LITERAL_STRING_
#define _H_NOIX_COMPILER_LITERAL_STRING_
#include "compiler/node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _noix_string_literal_node_t {
  struct _noix_ast_node_t node;
} *noix_string_literal_node_t;
noix_ast_node_t noix_read_string_literal(noix_allocator_t allocator,
                                         const char *file,
                                         noix_position_t *position);
#ifdef __cplusplus
}
#endif
#endif