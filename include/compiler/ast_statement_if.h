#ifndef _H_NEO_COMPILER_STATEMENT_IF__
#define _H_NEO_COMPILER_STATEMENT_IF__
#include "compiler/ast_node.h"
#include "core/allocator.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_statement_if_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t condition;
  neo_ast_node_t alternate;
  neo_ast_node_t consequent;
} *neo_ast_statement_if_t;

neo_ast_node_t neo_ast_read_statement_if(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif