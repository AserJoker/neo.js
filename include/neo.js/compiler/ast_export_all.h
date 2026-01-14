#ifndef _H_NEO_COMPILER_EXPORT_ALL_
#define _H_NEO_COMPILER_EXPORT_ALL_
#include "compiler/ast_node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_export_all_t {
  struct _neo_ast_node_t node;
} *neo_ast_export_all_t;

neo_ast_node_t neo_ast_read_export_all(neo_allocator_t allocator,
                                       const char *file,
                                       neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif