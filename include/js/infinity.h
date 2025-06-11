#ifndef _H_NEO_JS_INFINITY_
#define _H_NEO_JS_INFINITY_
#include "core/allocator.h"
#include "js/type.h"
#include "js/value.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_infinity_t *neo_js_infinity_t;

neo_js_type_t neo_get_js_infinity_type();

neo_js_infinity_t neo_create_js_infinity(neo_allocator_t allocator,
                                         bool negative);

neo_js_infinity_t neo_js_value_to_infinity(neo_js_value_t value);

neo_js_value_t neo_js_infinity_to_value(neo_js_infinity_t self);

bool neo_js_infinity_is_negative(neo_js_infinity_t self);

#ifdef __cplusplus
}
#endif
#endif