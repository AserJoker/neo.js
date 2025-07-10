#ifndef _H_NEO_COMPILER_IMPORT_DEFAULT_
#define _H_NEO_COMPILER_IMPORT_DEFAULT_
#include "compiler/ast/node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_import_default_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t identifier;
} *neo_ast_import_default_t;

neo_ast_node_t neo_ast_read_import_default(neo_allocator_t allocator,
                                           const wchar_t *file,
                                           neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif