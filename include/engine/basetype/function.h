#ifndef _H_NEO_ENGINE_BASETYPE_FUNCTION_
#define _H_NEO_ENGINE_BASETYPE_FUNCTION_
#include "core/allocator.h"
#include "engine/basetype/callable.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_engine_function_t *neo_engine_function_t;

struct _neo_engine_function_t {
  struct _neo_engine_callable_t callable;
  neo_engine_cfunction_fn_t callee;
};

neo_engine_type_t neo_get_js_function_type();

neo_engine_function_t neo_create_js_function(neo_allocator_t allocator,
                                             neo_engine_cfunction_fn_t callee);

neo_engine_function_t neo_engine_value_to_function(neo_engine_value_t value);

#ifdef __cplusplus
}
#endif
#endif