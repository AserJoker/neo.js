#ifndef _H_NEO_JS_FUNCTION_
#define _H_NEO_JS_FUNCTION_
#include "core/allocator.h"
#include "core/hash_map.h"
#include "js/handle.h"
#include "js/object.h"
#include "js/type.h"
#include "js/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_function_t *neo_js_function_t;

struct _neo_js_function_t {
  struct _neo_js_object_t object;
  neo_js_cfunction_fn_t callee;
  neo_hash_map_t closure;
  neo_js_handle_t name;
  wchar_t *source;
};

neo_js_type_t neo_get_js_function_type();

neo_js_function_t neo_create_js_function(neo_allocator_t allocator,
                                         neo_js_cfunction_fn_t callee);

neo_js_function_t neo_js_value_to_function(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif