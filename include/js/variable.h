#ifndef _H_NEO_JS_VARIABLE_
#define _H_NEO_JS_VARIABLE_
#include "core/allocator.h"
#include "js/basetype/array.h"
#include "js/basetype/boolean.h"
#include "js/basetype/function.h"
#include "js/basetype/null.h"
#include "js/basetype/number.h"
#include "js/basetype/string.h"
#include "js/basetype/undefined.h"
#include "js/handle.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_variable_t *neo_js_variable_t;

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_handle_t handle);

neo_js_handle_t neo_js_variable_get_handle(neo_js_variable_t variable);

static inline neo_js_value_t
neo_js_variable_get_value(neo_js_variable_t variable) {
  return neo_js_handle_get_value(neo_js_variable_get_handle(variable));
}

static inline neo_js_type_t
neo_js_variable_get_type(neo_js_variable_t variable) {
  return neo_js_variable_get_value(variable)->type;
}

bool neo_js_variable_is_primitive(neo_js_variable_t variable);

static inline neo_js_string_t
neo_js_variable_to_string(neo_js_variable_t variable) {
  return neo_js_value_to_string(neo_js_variable_get_value(variable));
}

static inline neo_js_number_t
neo_js_variable_to_number(neo_js_variable_t variable) {
  return neo_js_value_to_number(neo_js_variable_get_value(variable));
}

static inline neo_js_boolean_t
neo_js_variable_to_boolean(neo_js_variable_t variable) {
  return neo_js_value_to_boolean(neo_js_variable_get_value(variable));
}

static inline neo_js_null_t
neo_js_variable_to_null(neo_js_variable_t variable) {
  return neo_js_value_to_null(neo_js_variable_get_value(variable));
}

static inline neo_js_undefined_t
neo_js_variable_to_undefined(neo_js_variable_t variable) {
  return neo_js_value_to_undefined(neo_js_variable_get_value(variable));
}

static inline neo_js_function_t
neo_js_variable_to_function(neo_js_variable_t variable) {
  return neo_js_value_to_function(neo_js_variable_get_value(variable));
}

static inline neo_js_object_t
neo_js_variable_to_object(neo_js_variable_t variable) {
  return neo_js_value_to_object(neo_js_variable_get_value(variable));
}

static inline neo_js_array_t
neo_js_variable_to_array(neo_js_variable_t variable) {
  return neo_js_value_to_array(neo_js_variable_get_value(variable));
}

#ifdef __cplusplus
}
#endif
#endif