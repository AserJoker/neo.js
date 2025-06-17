#ifndef _H_NEO_JS_NULL_
#define _H_NEO_JS_NULL_
#include "core/allocator.h"
#include "js/type.h"
#include "js/value.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_null_t *neo_js_null_t;

struct _neo_js_null_t {
  struct _neo_js_value_t value;
};

neo_js_type_t neo_get_js_null_type();

neo_js_null_t neo_create_js_null(neo_allocator_t allocator);

neo_js_null_t neo_js_value_to_null(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif