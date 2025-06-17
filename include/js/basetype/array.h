#ifndef _H_NEO_JS_ARRAY_
#define _H_NEO_JS_ARRAY_
#include "core/allocator.h"
#include "js/basetype/object.h"
#include "js/type.h"
#include "js/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_array_t *neo_js_array_t;

struct _neo_js_array_t {
  struct _neo_js_object_t object;
  size_t length;
};

neo_js_type_t neo_get_js_array_type();

neo_js_array_t neo_create_js_array(neo_allocator_t allocator);

neo_js_array_t neo_js_value_to_array(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif