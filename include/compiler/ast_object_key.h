#ifndef _H_NEO_COMPILER_OBJECT_KEY__
#define _H_NEO_COMPILER_OBJECT_KEY__
#include "compiler/ast_node.h"
#include "core/allocator.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

neo_ast_node_t neo_ast_read_object_key(neo_allocator_t allocator,
                                       const char *file,
                                       neo_position_t *position);

neo_ast_node_t neo_ast_read_object_computed_key(neo_allocator_t allocator,
                                                const char *file,
                                                neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif