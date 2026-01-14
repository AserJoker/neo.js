#ifndef _H_NEO_COMPILER_VARIABLE_DECLARATOR_
#define _H_NEO_COMPILER_VARIABLE_DECLARATOR_
#include "neo.js/compiler/ast_node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_variable_declarator_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t identifier;
  neo_ast_node_t initialize;
} *neo_ast_variable_declarator_t;

neo_ast_node_t neo_ast_read_variable_declarator(neo_allocator_t allocator,
                                                const char *file,
                                                neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif