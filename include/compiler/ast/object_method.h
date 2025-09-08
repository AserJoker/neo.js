#ifndef _H_NEO_COMPILER_OBJECT_METHOD__
#define _H_NEO_COMPILER_OBJECT_METHOD__
#include "compiler/ast/node.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_object_method_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t name;
  neo_list_t arguments;
  neo_ast_node_t body;
  bool async;
  bool generator;
  bool computed;
  neo_list_t closure;
} *neo_ast_object_method_t;

neo_ast_node_t neo_ast_read_object_method(neo_allocator_t allocator,
                                          const char *file,
                                          neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif