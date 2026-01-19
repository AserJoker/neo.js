#ifndef _H_NEO_COMPILER_PRIVATE_NAME_
#define _H_NEO_COMPILER_PRIVATE_NAME_
#include "neojs/compiler/ast_node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_private_name_t {
  struct _neo_ast_node_t node;
} *neo_ast_private_name_t;

neo_ast_node_t neo_ast_read_private_name(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif