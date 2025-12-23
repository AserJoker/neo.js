#ifndef _H_NEO_COMPILER_IDENTIFIER_
#define _H_NEO_COMPILER_IDENTIFIER_
#include "compiler/ast_node.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_identifier_t {
  struct _neo_ast_node_t node;
} *neo_ast_identifier_t;

neo_ast_node_t neo_ast_read_identifier(neo_allocator_t allocator,
                                       const char *file,
                                       neo_position_t *position);

neo_ast_node_t neo_ast_read_identifier_compat(neo_allocator_t allocator,
                                              const char *file,
                                              neo_position_t *position);

bool neo_is_keyword(neo_location_t location);
#ifdef __cplusplus
}
#endif
#endif