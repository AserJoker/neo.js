#ifndef _H_NEO_COMPILER_TRY_CATCH__
#define _H_NEO_COMPILER_TRY_CATCH__
#include "compiler/ast_node.h"
#include "core/allocator.h"
#include "core/position.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_ast_try_catch_t {
  struct _neo_ast_node_t node;
  neo_ast_node_t body;
  neo_ast_node_t error;
} *neo_ast_try_catch_t;

neo_ast_node_t neo_ast_read_try_catch(neo_allocator_t allocator,
                                      const char *file,
                                      neo_position_t *position);
#ifdef __cplusplus
}
#endif
#endif