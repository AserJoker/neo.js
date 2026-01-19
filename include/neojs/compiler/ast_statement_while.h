#ifndef _H_NEO_COMPILER_STATEMENT_WHILE__
#define _H_NEO_COMPILER_STATEMENT_WHILE__
#include "neojs/compiler/ast_node.h"
#include "neojs/core/allocator.h"
#include "neojs/core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_statement_while_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t condition;
  neo_ast_node_t body;
} *neo_ast_statement_while_t;

neo_ast_node_t neo_ast_read_statement_while(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif