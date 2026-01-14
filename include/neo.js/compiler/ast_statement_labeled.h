#ifndef _H_NEO_COMPILER_STATEMENT_LABELED__
#define _H_NEO_COMPILER_STATEMENT_LABELED__
#include "neo.js/compiler/ast_node.h"
#include "neo.js/core/allocator.h"
#include "neo.js/core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_statement_labeled_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t label;
  neo_ast_node_t statement;
} *neo_ast_statement_labeled_t;

neo_ast_node_t neo_ast_read_statement_labeled(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif