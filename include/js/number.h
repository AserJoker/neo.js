#ifndef _H_NEO_JS_NUMBER_
#define _H_NEO_JS_NUMBER_
#include "core/allocator.h"
#include "js/type.h"
#include "js/value.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _neo_js_number_t *neo_js_number_t;

neo_js_number_t neo_create_js_number(neo_allocator_t allocator, double value);

neo_js_type_t neo_get_js_number_type();

neo_js_value_t neo_js_number_to_value(neo_js_number_t self);

neo_js_number_t neo_js_value_to_number(neo_js_value_t value);

double neo_js_number_get_value(neo_js_number_t self);

#ifdef __cplusplus
}
#endif
#endif