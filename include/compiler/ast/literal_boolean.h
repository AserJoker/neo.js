#ifndef _H_NEO_COMPILER_LITERAL_BOOLEAN_
#define _H_NEO_COMPILER_LITERAL_BOOLEAN_
#include "compiler/ast/node.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_literal_boolean_t {
  struct _neo_ast_node_t node;
} *neo_ast_literal_boolean_t;

neo_ast_node_t neo_ast_read_literal_boolean(neo_allocator_t allocator,
                                            const wchar_t *file,
                                            neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif