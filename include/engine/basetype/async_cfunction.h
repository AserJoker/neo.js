#ifndef _H_NEO_ENGINE_BASETYPE_ASYNC_CFUNCTION_
#define _H_NEO_ENGINE_BASETYPE_ASYNC_CFUNCTION_
#include "core/allocator.h"
#include "engine/basetype/callable.h"
#include "engine/type.h"
#include "engine/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_async_cfunction_t *neo_js_async_cfunction_t;

struct _neo_js_async_cfunction_t {
  struct _neo_js_callable_t callable;
  neo_js_async_cfunction_fn_t callee;
};

neo_js_type_t neo_get_js_async_cfunction_type();

neo_js_async_cfunction_t
neo_create_js_async_cfunction(neo_allocator_t allocator,
                              neo_js_async_cfunction_fn_t callee);

neo_js_async_cfunction_t neo_js_value_to_async_cfunction(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif