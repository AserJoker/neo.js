#ifndef _H_NEO_COMPILER_STATEMENT_FOR__
#define _H_NEO_COMPILER_STATEMENT_FOR__
#include "compiler/ast_node.h"
#include "core/allocator.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_statement_for_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t initialize;
  neo_ast_node_t condition;
  neo_ast_node_t body;
  neo_ast_node_t after;
} *neo_ast_statement_for_t;

neo_ast_node_t neo_ast_read_statement_for(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif