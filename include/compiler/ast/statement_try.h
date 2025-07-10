#ifndef _H_NEO_COMPILER_STATEMENT_TRY__
#define _H_NEO_COMPILER_STATEMENT_TRY__
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_statement_try_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t body;
  neo_ast_node_t catch;
  neo_ast_node_t finally;
} *neo_ast_statement_try_t;

neo_ast_node_t neo_ast_read_statement_try(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif