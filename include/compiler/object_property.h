#ifndef _H_NEO_COMPILER_OBJECT_PROPERTY__
#define _H_NEO_COMPILER_OBJECT_PROPERTY__
#include "compiler/node.h"
#include "core/allocator.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_object_property_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t identifier;
  neo_ast_node_t value;
  bool computed;
} *neo_ast_object_property_t;

neo_ast_node_t neo_ast_read_object_property(neo_allocator_t allocator,
                                            const char *file,
                                            neo_position_t *position);

#ifdef __cplusplus
}
#endif
#endif