#ifndef _H_NEO_ENGINE_VARIABLE_
#define _H_NEO_ENGINE_VARIABLE_
#include "core/allocator.h"
#include "engine/basetype/array.h"
#include "engine/basetype/boolean.h"
#include "engine/basetype/cfunction.h"
#include "engine/basetype/coroutine.h"
#include "engine/basetype/error.h"
#include "engine/basetype/function.h"
#include "engine/basetype/interrupt.h"
#include "engine/basetype/null.h"
#include "engine/basetype/number.h"
#include "engine/basetype/string.h"
#include "engine/basetype/symbol.h"
#include "engine/basetype/undefined.h"
#include "engine/handle.h"
#include "engine/type.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct _neo_js_variable_t *neo_js_variable_t;

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_handle_t handle);

neo_js_handle_t neo_js_variable_get_handle(neo_js_variable_t variable);

void neo_js_variable_set_const(neo_js_variable_t variable, bool is_const);

bool neo_js_variable_is_const(neo_js_variable_t variable);

void neo_js_variable_set_using(neo_js_variable_t variable, bool is_using);

bool neo_js_variable_is_using(neo_js_variable_t variable);

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

static inline neo_js_symbol_t
neo_js_variable_to_symbol(neo_js_variable_t variable) {
  return neo_js_value_to_symbol(neo_js_variable_get_value(variable));
}

static inline neo_js_cfunction_t
neo_js_variable_to_cfunction(neo_js_variable_t variable) {
  return neo_js_value_to_cfunction(neo_js_variable_get_value(variable));
}

static inline neo_js_function_t
neo_js_variable_to_function(neo_js_variable_t variable) {
  return neo_js_value_to_function(neo_js_variable_get_value(variable));
}

static inline neo_js_callable_t
neo_js_variable_to_callable(neo_js_variable_t variable) {
  return neo_js_value_to_callable(neo_js_variable_get_value(variable));
}

static inline neo_js_object_t
neo_js_variable_to_object(neo_js_variable_t variable) {
  return neo_js_value_to_object(neo_js_variable_get_value(variable));
}

static inline neo_js_array_t
neo_js_variable_to_array(neo_js_variable_t variable) {
  return neo_js_value_to_array(neo_js_variable_get_value(variable));
}
static inline neo_js_error_t
neo_js_variable_to_error(neo_js_variable_t variable) {
  return neo_js_value_to_error(neo_js_variable_get_value(variable));
}

static inline neo_js_interrupt_t
neo_js_variable_to_interrupt(neo_js_variable_t variable) {
  return neo_js_value_to_interrupt(neo_js_variable_get_value(variable));
}
static inline neo_js_coroutine_t
neo_js_variable_to_coroutine(neo_js_variable_t variable) {
  return neo_js_value_to_coroutine(neo_js_variable_get_value(variable));
}

#ifdef __cplusplus
}
#endif
#endif