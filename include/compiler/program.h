#ifndef _H_NOIX_COMPILER_PROGRAM__
#define _H_NOIX_COMPILER_PROGRAM__
#include "compiler/node.h"
#include "core/list.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _noix_ast_program_node_t {
  struct _noix_ast_node_t node;
  noix_ast_node_t interpreter;
  noix_list_t body;
  noix_list_t directives;
} *noix_ast_program_node_t;

noix_ast_node_t noix_ast_read_program(noix_allocator_t allocator,
                                      const char *file,
                                      noix_position_t *position);
#ifdef __cplusplus
}
#endif
#endif