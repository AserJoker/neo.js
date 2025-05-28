#ifndef _H_NEO_COMPILER_CLASS_ACCESSOR__
#define _H_NEO_COMPILER_CLASS_ACCESSOR__
#include "compiler/node.h"
#include "compiler/object_accessor.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_class_accessor_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t name;
  neo_list_t arguments;
  neo_ast_node_t body;
  neo_accessor_kind_t kind;
  neo_list_t decorators;
  bool computed;
  bool static_;
  neo_list_t closure;
} *neo_ast_class_accessor_t;

neo_ast_node_t neo_ast_read_class_accessor(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif