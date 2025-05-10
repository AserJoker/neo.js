#ifndef _H_NEO_COMPILER_PATTERN_OBJECT_
#define _H_NEO_COMPILER_PATTERN_OBJECT_
#include "compiler/node.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/position.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_pattern_object_t {
  struct _neo_ast_node_t node;
  neo_list_t items;
} *neo_ast_pattern_object_t;

neo_ast_node_t neo_ast_read_pattern_object(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif