#ifndef _H_NOIX_COMPILER_INTERPRETER_
#define _H_NOIX_COMPILER_INTERPRETER_
#include "compiler/node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _noix_interpreter_node_t {
  struct _noix_ast_node_t node;
} *noix_interpreter_node_t;

noix_ast_node_t noix_read_interpreter(noix_allocator_t allocator,
                                      const char *file,
                                      noix_position_t *position);
#ifdef __cplusplus
}
#endif
#endif