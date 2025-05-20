#ifndef _H_NEO_COMPILER_SCOPE_
#define _H_NEO_COMPILER_SCOPE_
#include "core/allocator.h"
#include "core/list.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_compile_scope_t *neo_compile_scope_t;

typedef struct _neo_compile_variable_t *neo_compile_variable_t;

typedef enum _neo_compile_scope_type_t {
  NEO_COMPILE_SCOPE_BLOCK,
  NEO_COMPILE_SCOPE_FUNCTION,
} neo_compile_scope_type_t;

typedef enum _neo_compile_variable_type_t {
  NEO_COMPILE_VARIABLE_VAR,
  NEO_COMPILE_VARIABLE_LET,
  NEO_COMPILE_VARIABLE_CONST,
  NEO_COMPILE_VARIABLE_FUNCTION,
} neo_compile_variable_type_t;

struct _neo_compile_variable_t {
  neo_compile_variable_type_t type;
  char *name;
};

struct _neo_compile_scope_t {
  neo_compile_scope_t parent;
  neo_list_t variables;
  neo_list_t bindings;
};

neo_compile_scope_t neo_compile_scope_push(neo_allocator_t allocator);

neo_compile_scope_t neo_compile_scope_pop(neo_compile_scope_t scope);

void neo_compile_declar_value(neo_allocator_t allocator, const char *name,
                              neo_compile_variable_type_t type);

void neo_compile_bind_variable(neo_allocator_t allocator, const char *name);

#ifdef __cplusplus
}
#endif
#endif