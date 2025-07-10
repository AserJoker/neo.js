#ifndef _H_NEO_COMPILER_STATEMENT_FOR_IN__
#define _H_NEO_COMPILER_STATEMENT_FOR_IN__
#include "compiler/ast/declaration_variable.h"
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_statement_for_in_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t left;
  neo_ast_node_t right;
  neo_ast_node_t body;
  neo_ast_declaration_kind_t kind;
} *neo_ast_statement_for_in_t;

neo_ast_node_t neo_ast_read_statement_for_in(neo_allocator_t allocator,
                                             const wchar_t *file,
                                             neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif