#ifndef _H_NEO_COMPILER_FUNCTION_BODY__
#define _H_NEO_COMPILER_FUNCTION_BODY__
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_function_body_t {
  struct _neo_ast_node_t node;
  neo_list_t directives;
  neo_list_t body;
} *neo_ast_function_body_t;

neo_ast_node_t neo_ast_read_function_body(neo_allocator_t allocator,
                                          const wchar_t *file,
                                          neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif