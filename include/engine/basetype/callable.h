#ifndef _H_NEO_ENGINE_BASETYPE_CALLABLE_
#define _H_NEO_ENGINE_BASETYPE_CALLABLE_
#include "core/hash_map.h"
#include "engine/basetype/object.h"
#include "engine/chunk.h"
#include "engine/type.h"
#include "engine/value.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_callable_t *neo_js_callable_t;

struct _neo_js_callable_t {
  struct _neo_js_object_t object;
  neo_hash_map_t closure;
  wchar_t *name;
  neo_js_chunk_t bind;
  neo_js_chunk_t clazz;
  bool is_class;
};

neo_js_callable_t neo_js_value_to_callable(neo_js_value_t value);

void neo_js_callable_set_closure(neo_js_context_t ctx, neo_js_variable_t self,
                                 const wchar_t *name,
                                 neo_js_variable_t closure);

neo_js_variable_t neo_js_callable_get_closure(neo_js_context_t ctx,
                                              neo_js_variable_t self,
                                              const wchar_t *name);

neo_js_variable_t neo_js_callable_set_bind(neo_js_context_t ctx,
                                           neo_js_variable_t self,
                                           neo_js_variable_t bind);

neo_js_variable_t neo_js_callable_get_bind(neo_js_context_t ctx,
                                           neo_js_variable_t self);

neo_js_variable_t neo_js_callable_set_class(neo_js_context_t ctx,
                                            neo_js_variable_t self,
                                            neo_js_variable_t clazz);

neo_js_variable_t neo_js_callable_get_class(neo_js_context_t ctx,
                                            neo_js_variable_t self);

#ifdef __cplusplus
}
#endif
#endif