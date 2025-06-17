#ifndef _H_NEO_JS_CALLABLE_
#define _H_NEO_JS_CALLABLE_
#include "core/hash_map.h"
#include "js/basetype/object.h"
#include "js/handle.h"

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

#ifdef __cplusplus
}
#endif
#endif