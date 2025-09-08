#ifndef _H_NEO_COMPILER_DECORATOR_
#define _H_NEO_COMPILER_DECORATOR_
#include "compiler/ast/node.h"
#include "core/list.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_decorator_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t callee;
  neo_list_t arguments;
} *neo_ast_decorator_t;

neo_ast_node_t neo_ast_read_decorator(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif