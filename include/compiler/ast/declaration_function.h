#ifndef _H_NEO_COMPILER_DECLARATION_FUNCTION_
#define _H_NEO_COMPILER_DECLARATION_FUNCTION_
#include "compiler/ast/node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_declaration_function_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t declaration;
} *neo_ast_declaration_function_t;

neo_ast_node_t neo_ast_read_declaration_function(neo_allocator_t allocator,
                                                 const char *file,
                                                 neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif