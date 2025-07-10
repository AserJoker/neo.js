#ifndef _H_NEO_ENGINE_BASETYPE_FUNCTION_
#define _H_NEO_ENGINE_BASETYPE_FUNCTION_
#include "compiler/program.h"
#include "core/allocator.h"
#include "engine/basetype/callable.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_function_t *neo_js_function_t;
struct _neo_js_function_t {
  struct _neo_js_callable_t callable;
  size_t address;
  neo_program_t program;
  const wchar_t *source;
  bool is_async;
  bool is_generator;
};

neo_js_type_t neo_get_js_function_type();

neo_js_function_t neo_create_js_function(neo_allocator_t allocator,
                                         neo_program_t program);

neo_js_function_t neo_js_value_to_function(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif