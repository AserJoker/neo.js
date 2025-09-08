#ifndef _H_NEO_COMPILER_INTERPRETER_
#define _H_NEO_COMPILER_INTERPRETER_
#include "compiler/ast/node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_interpreter_t {
  struct _neo_ast_node_t node;
} *neo_ast_interpreter_t;

neo_ast_node_t neo_ast_read_interpreter(neo_allocator_t allocator,
                                        const char *file,
                                        neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif