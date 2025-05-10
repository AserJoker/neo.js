#ifndef _H_NEO_COMPILER_EXPRESSION_GROUP__
#define _H_NEO_COMPILER_EXPRESSION_GROUP__
#include "compiler/node.h"
#include "core/allocator.h"
#include "core/position.h"
#ifdef __cplusplus
extern "C" {
#endif

neo_ast_node_t neo_ast_read_expression_group(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif