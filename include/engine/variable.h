#ifndef _H_NEO_ENGINE_VARIABLE_
#define _H_NEO_ENGINE_VARIABLE_
#include "core/allocator.h"
#include "engine/basetype/array.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/function.h"
#include "engine/basetype/null.h"
#include "engine/basetype/number.h"
#include "engine/basetype/string.h"
#include "engine/basetype/symbol.h"
#include "engine/basetype/undefined.h"
#include "engine/handle.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_engine_variable_t *neo_engine_variable_t;

neo_engine_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                             neo_engine_handle_t handle);

neo_engine_handle_t
neo_engine_variable_get_handle(neo_engine_variable_t variable);

static inline neo_engine_value_t
neo_engine_variable_get_value(neo_engine_variable_t variable) {
  return neo_engine_handle_get_value(neo_engine_variable_get_handle(variable));
}

static inline neo_engine_type_t
neo_engine_variable_get_type(neo_engine_variable_t variable) {
  return neo_engine_variable_get_value(variable)->type;
}

bool neo_engine_variable_is_primitive(neo_engine_variable_t variable);

static inline neo_engine_string_t
neo_engine_variable_to_string(neo_engine_variable_t variable) {
  return neo_engine_value_to_string(neo_engine_variable_get_value(variable));
}

static inline neo_engine_number_t
neo_engine_variable_to_number(neo_engine_variable_t variable) {
  return neo_engine_value_to_number(neo_engine_variable_get_value(variable));
}

static inline neo_engine_boolean_t
neo_engine_variable_to_boolean(neo_engine_variable_t variable) {
  return neo_engine_value_to_boolean(neo_engine_variable_get_value(variable));
}

static inline neo_engine_null_t
neo_engine_variable_to_null(neo_engine_variable_t variable) {
  return neo_engine_value_to_null(neo_engine_variable_get_value(variable));
}

static inline neo_engine_undefined_t
neo_engine_variable_to_undefined(neo_engine_variable_t variable) {
  return neo_engine_value_to_undefined(neo_engine_variable_get_value(variable));
}

static inline neo_engine_symbol_t
neo_engine_variable_to_symbol(neo_engine_variable_t variable) {
  return neo_engine_value_to_symbol(neo_engine_variable_get_value(variable));
}

static inline neo_engine_function_t
neo_engine_variable_to_function(neo_engine_variable_t variable) {
  return neo_engine_value_to_function(neo_engine_variable_get_value(variable));
}

static inline neo_engine_object_t
neo_engine_variable_to_object(neo_engine_variable_t variable) {
  return neo_engine_value_to_object(neo_engine_variable_get_value(variable));
}

static inline neo_engine_array_t
neo_engine_variable_to_array(neo_engine_variable_t variable) {
  return neo_engine_value_to_array(neo_engine_variable_get_value(variable));
}

#ifdef __cplusplus
}
#endif
#endif