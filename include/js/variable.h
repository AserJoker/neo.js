#ifndef _H_NEO_JS_VARIABLE_
#define _H_NEO_JS_VARIABLE_
#include "core/allocator.h"
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

#ifdef __cplusplus
}
#endif
#endif