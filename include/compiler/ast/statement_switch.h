#ifndef _H_NEO_COMPILER_STATEMENT_SWITCH__
#define _H_NEO_COMPILER_STATEMENT_SWITCH__
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_statement_switch_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t condition;
  neo_list_t cases;
} *neo_ast_statement_switch_t;

neo_ast_node_t neo_ast_read_statement_switch(neo_allocator_t allocator,
                                             const wchar_t *file,
                                             neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif