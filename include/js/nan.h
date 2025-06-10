#ifndef _H_NEO_JS_NAN_
#define _H_NEO_JS_NAN_
#include "core/allocator.h"
#include "js/type.h"
#include "js/value.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_nan_t *neo_js_nan_t;

neo_js_nan_t neo_create_js_nan(neo_allocator_t allocator);

neo_js_type_t neo_get_js_nan_type();

neo_js_value_t neo_js_nan_to_value(neo_js_nan_t self);

neo_js_nan_t neo_js_value_to_nan(neo_js_value_t value);

#ifdef __cplusplus
}
#endif
#endif