#ifndef _H_NEO_ENGINE_BASETYPE_CALLABLE_
#define _H_NEO_ENGINE_BASETYPE_CALLABLE_
#include "core/hash_map.h"
#include "engine/basetype/object.h"
#include "engine/handle.h"
#include "engine/value.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_callable_t *neo_js_callable_t;

struct _neo_js_callable_t {
  struct _neo_js_object_t object;
  neo_hash_map_t closure;
  neo_js_handle_t name;
  wchar_t *source;
};
neo_js_callable_t neo_js_value_to_callable(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif