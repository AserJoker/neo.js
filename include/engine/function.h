#ifndef _H_NEO_ENGINE_FUNCTION_
#define _H_NEO_ENGINE_FUNCTION_
#include "compiler/program.h"
#include "core/allocator.h"
#include "engine/callable.h"
#ifdef __cplusplus
extern "C" {
#endif
struct _neo_js_function_t {
  struct _neo_js_callable_t super;
  neo_program_t program;
  size_t address;
  const char *source;
};
typedef struct _neo_js_function_t *neo_js_function_t;
neo_js_function_t neo_create_js_function(neo_allocator_t allocator,
                                         neo_program_t program,
                                         neo_js_value_t prototype);
void neo_init_js_function(neo_js_function_t self, neo_allocator_t allocaotr,
                          neo_program_t program, neo_js_value_t prototype);
void neo_deinit_js_function(neo_js_function_t self, neo_allocator_t allocaotr);
neo_js_value_t neo_js_function_to_value(neo_js_function_t self);

#ifdef __cplusplus
}
#endif
#endif