#ifndef _H_NEO_COMPILER_LITERAL_TEMPLATE_
#define _H_NEO_COMPILER_LITERAL_TEMPLATE_
#include "neo.js/compiler/ast_node.h"
#include "neo.js/core/list.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_literal_template_t {
  struct _neo_ast_node_t node;
  neo_list_t quasis;
  neo_list_t expressions;
  neo_ast_node_t tag;
} *neo_ast_literal_template_t;

neo_ast_node_t neo_ast_read_literal_template(neo_allocator_t allocator,
                                             const char *file,
                                             neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif