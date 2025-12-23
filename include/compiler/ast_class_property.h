#ifndef _H_NEO_COMPILER_CLASS_PROPERTY__
#define _H_NEO_COMPILER_CLASS_PROPERTY__
#include "compiler/ast_node.h"
#include "core/allocator.h"
#include "core/list.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_class_property_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t identifier;
  neo_ast_node_t value;
  neo_list_t decorators;
  bool computed;
  bool static_;
  bool accessor;
} *neo_ast_class_property_t;

neo_ast_node_t neo_ast_read_class_property(neo_allocator_t allocator,
                                           const char *file,
                                           neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif