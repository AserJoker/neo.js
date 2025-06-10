#ifndef _H_NEO_COMPILER_DECLARATION_VARIABLE_
#define _H_NEO_COMPILER_DECLARATION_VARIABLE_
#include "compiler/ast/node.h"
#include "core/list.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum _neo_ast_declaration_kind_t {
  NEO_AST_DECLARATION_VAR,
  NEO_AST_DECLARATION_CONST,
  NEO_AST_DECLARATION_USING,
  NEO_AST_DECLARATION_LET,
  NEO_AST_DECLARATION_NONE,
} neo_ast_declaration_kind_t;

typedef struct _neo_ast_declaration_variable_t {
  struct _neo_ast_node_t node;
  neo_list_t declarators;
  neo_ast_declaration_kind_t kind;
} *neo_ast_declaration_variable_t;

neo_ast_node_t neo_ast_read_declaration_variable(neo_allocator_t allocator,
                                                 const char *file,
                                                 neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif