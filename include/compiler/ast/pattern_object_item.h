#ifndef _H_NEO_COMPILER_PATTERN_OBJECT_ITEM_
#define _H_NEO_COMPILER_PATTERN_OBJECT_ITEM_
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include "core/position.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_pattern_object_item_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t identifier;
  neo_ast_node_t alias;
  neo_ast_node_t value;
} *neo_ast_pattern_object_item_t;

neo_ast_node_t neo_ast_read_pattern_object_item(neo_allocator_t allocator,
                                                const char *file,
                                                neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif