#ifndef _H_NEO_ENGINE_BASETYPE_CALLABLE_
#define _H_NEO_ENGINE_BASETYPE_CALLABLE_
#include "core/hash_map.h"
#include "engine/basetype/object.h"
#include "engine/handle.h"
#include "engine/type.h"
#include "engine/value.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_callable_t *neo_js_callable_t;

struct _neo_js_callable_t {
  struct _neo_js_object_t object;
  neo_hash_map_t closure;
  neo_js_handle_t name;
  neo_js_handle_t bind;
};

neo_js_callable_t neo_js_value_to_callable(neo_js_value_t value);

void neo_js_callable_set_closure(neo_js_context_t ctx, neo_js_variable_t self,
                                 const wchar_t *name,
                                 neo_js_variable_t closure);

neo_js_variable_t neo_js_callable_get_closure(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              const wchar_t *name);

#ifdef __cplusplus
}
#endif
#endif