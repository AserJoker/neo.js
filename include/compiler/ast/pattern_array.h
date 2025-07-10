#ifndef _H_NEO_COMPILER_PATTERN_ARRAY_
#define _H_NEO_COMPILER_PATTERN_ARRAY_
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/position.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_pattern_array_t {
  struct _neo_ast_node_t node;
  neo_list_t items;
} *neo_ast_pattern_array_t;

neo_ast_node_t neo_ast_read_pattern_array(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif