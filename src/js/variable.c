#include "js/variable.h"
#include "core/allocator.h"
#include "js/boolean.h"
#include "js/handle.h"
#include "js/infinity.h"
#include "js/nan.h"
#include "js/null.h"
#include "js/number.h"
#include "js/string.h"
#include "js/symbol.h"
#include "js/type.h"
#include "js/undefined.h"
struct _neo_js_variable_t {
  neo_js_handle_t handle;
};

static void neo_js_variable_dispose(neo_allocator_t allocator,
                                    neo_js_variable_t variable) {}

neo_js_variable_t neo_create_js_variable(neo_allocator_t allocator,
                                         neo_js_handle_t handle) {
  neo_js_variable_t variable = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_variable_t), neo_js_variable_dispose);
  variable->handle = handle;
  return variable;
}

neo_js_handle_t neo_js_variable_get_handle(neo_js_variable_t variable) {
  return variable->handle;
}

bool neo_js_variable_is_number(neo_js_variable_t variable) {
  neo_js_type_t type = neo_js_variable_get_type(variable);
  return type == neo_get_js_number_type() || type == neo_get_js_nan_type() ||
         type == neo_get_js_infinity_type();
}

bool neo_js_variable_is_primitive(neo_js_variable_t variable) {
  neo_js_type_t type = neo_js_variable_get_type(variable);
  return neo_js_variable_is_number(variable) ||
         type == neo_get_js_string_type() ||
         type == neo_get_js_boolean_type() ||
         type == neo_get_js_undefined_type() ||
         type == neo_get_js_null_type() || type == neo_get_js_symbol_type();
}