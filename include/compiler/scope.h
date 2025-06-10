#ifndef _H_NEO_COMPILER_SCOPE_
#define _H_NEO_COMPILER_SCOPE_
#include "core/allocator.h"
#include "core/list.h"
#include "core/variable.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_compile_scope_t *neo_compile_scope_t;

typedef struct _neo_compile_variable_t *neo_compile_variable_t;

typedef struct _neo_ast_node_t *neo_ast_node_t;

typedef enum _neo_compile_scope_type_t {
  NEO_COMPILE_SCOPE_BLOCK,
  NEO_COMPILE_SCOPE_FUNCTION,
} neo_compile_scope_type_t;

typedef enum _neo_compile_variable_type_t {
  NEO_COMPILE_VARIABLE_VAR,
  NEO_COMPILE_VARIABLE_LET,
  NEO_COMPILE_VARIABLE_CONST,
  NEO_COMPILE_VARIABLE_USING,
  NEO_COMPILE_VARIABLE_FUNCTION,
} neo_compile_variable_type_t;

struct _neo_compile_variable_t {
  neo_compile_variable_type_t type;
  neo_ast_node_t node;
};

struct _neo_compile_scope_t {
  neo_compile_scope_t parent;
  neo_list_t variables;
  neo_compile_scope_type_t type;
};

neo_compile_scope_t neo_compile_scope_push(neo_allocator_t allocator,
                                           neo_compile_scope_type_t type);

neo_compile_scope_t neo_compile_scope_pop(neo_compile_scope_t scope);

void neo_compile_scope_declar_value(neo_allocator_t allocator,
                                    neo_compile_scope_t self,
                                    neo_ast_node_t node,
                                    neo_compile_variable_type_t type);

void neo_compile_scope_declar(neo_allocator_t allocator,
                              neo_compile_scope_t self, neo_ast_node_t node,
                              neo_compile_variable_type_t type);

neo_compile_scope_t neo_compile_scope_get_current();

neo_variable_t neo_serialize_scope(neo_allocator_t allocator,
                                   neo_compile_scope_t scope);

#ifdef __cplusplus
}
#endif
#endif