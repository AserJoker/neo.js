#ifndef _H_NEO_COMPILER_IMPORT_SPECIFIER_
#define _H_NEO_COMPILER_IMPORT_SPECIFIER_
#include "compiler/node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_import_specifier_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t alias;
  neo_ast_node_t identifier;
} *neo_ast_import_specifier_t;

neo_ast_node_t neo_ast_read_import_specifier(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif