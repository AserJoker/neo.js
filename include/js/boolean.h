#ifndef _H_NEO_JS_BOOLEAN_
#define _H_NEO_JS_BOOLEAN_
#include "core/allocator.h"
#include "js/type.h"
#include "js/value.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_boolean_t *neo_js_boolean_t;

neo_js_boolean_t neo_create_js_boolean(neo_allocator_t allocator, bool value);

neo_js_type_t neo_get_js_boolean_type();

neo_js_value_t neo_js_boolean_to_value(neo_js_boolean_t self);

neo_js_boolean_t neo_js_value_to_boolean(neo_js_value_t value);

bool neo_js_boolean_get_value(neo_js_boolean_t self);

#ifdef __cplusplus
}
#endif
#endif