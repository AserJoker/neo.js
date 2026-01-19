#ifndef _H_NEO_COMPILER_CLASS_METHOD__
#define _H_NEO_COMPILER_CLASS_METHOD__
#include "neojs/compiler/ast_node.h"
#include "neojs/core/allocator.h"
#include "neojs/core/list.h"
#include "neojs/core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_class_method_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t name;
  neo_list_t arguments;
  neo_list_t decorators;
  neo_ast_node_t body;
  bool async;
  bool generator;
  bool computed;
  bool static_;
  neo_list_t closure;
} *neo_ast_class_method_t;

neo_ast_node_t neo_ast_read_class_method(neo_allocator_t allocator,
                                         const char *file,
                                         neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif