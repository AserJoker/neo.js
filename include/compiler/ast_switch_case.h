#ifndef _H_NEO_COMPILER_SWITCH_CASE_
#define _H_NEO_COMPILER_SWITCH_CASE_
#include "compiler/ast_node.h"
#include "core/list.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_ast_switch_case_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t condition;
  neo_list_t body;
} *neo_ast_switch_case_t;

neo_ast_node_t neo_ast_read_switch_case(neo_allocator_t allocator,
                                        const char *file,
                                        neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif