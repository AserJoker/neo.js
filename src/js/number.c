#include "js/number.h"
#include "core/allocator.h"
#include "js/context.h"
#include "js/handle.h"
#include "js/runtime.h"
#include "js/string.h"
#include "js/type.h"
#include "js/value.h"
#include "js/variable.h"

struct _neo_js_number_t {
  struct _neo_js_value_t value;
  double number;
};

static const wchar_t *neo_js_number_typeof(neo_js_context_t ctx,
                                           neo_js_variable_t variable) {
  return L"number";
}

static neo_js_variable_t neo_js_number_to_string(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_number_t number = neo_js_value_to_number(
      neo_js_handle_get_value(neo_js_variable_get_handle(self)));
  wchar_t str[32];
  swprintf(str, 32, L"%lf", number->number);
  neo_js_string_t string = neo_create_js_string(allocator, str);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, neo_js_string_to_value(string)));
}

static neo_js_variable_t neo_js_number_to_boolean(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  neo_js_number_t number =
      neo_js_value_to_number(neo_js_variable_get_value(self));
  return neo_js_context_create_boolean(ctx, number->number != 0);
}

neo_js_type_t neo_get_js_number_type() {
  static struct _neo_js_type_t type = {
      neo_js_number_typeof,
      neo_js_number_to_string,
      neo_js_number_to_boolean,
  };
  return &type;
}

static void neo_js_number_dispose(neo_allocator_t allocator,
                                  neo_js_number_t self) {}

neo_js_number_t neo_create_js_number(neo_allocator_t allocator, double value) {
  neo_js_number_t number = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_number_t), neo_js_number_dispose);
  number->value.type = neo_get_js_number_type();
  number->number = value;
  return number;
}

neo_js_value_t neo_js_number_to_value(neo_js_number_t self) {
  return &self->value;
}

neo_js_number_t neo_js_value_to_number(neo_js_value_t value) {
  if (value->type == neo_get_js_number_type()) {
    return (neo_js_number_t)value;
  }
  return NULL;
}

double neo_js_number_get_value(neo_js_number_t self) { return self->number; }