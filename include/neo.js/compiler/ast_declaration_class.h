#ifndef _H_NEO_COMPILER_DECLARATION_CLASS_
#define _H_NEO_COMPILER_DECLARATION_CLASS_
#include "neo.js/compiler/ast_node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_declaration_class_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t declaration;
} *neo_ast_declaration_class_t;

neo_ast_node_t neo_ast_read_declaration_class(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif