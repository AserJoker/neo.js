#ifndef _H_NEO_COMPILER_PROGRAM__
#define _H_NEO_COMPILER_PROGRAM__
#include "compiler/ast/node.h"
#include "core/list.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_program_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t interpreter;
  neo_list_t body;
  neo_list_t directives;
} *neo_ast_program_t;

neo_ast_node_t neo_ast_read_program(neo_allocator_t allocator, const char *file,
                                    neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif