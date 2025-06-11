#include "js/infinity.h"
#include "core/allocator.h"
#include "js/context.h"
#include "js/handle.h"
#include "js/runtime.h"
#include "js/string.h"
#include "js/type.h"
#include "js/value.h"
#include "js/variable.h"
#include <stdbool.h>

struct _neo_js_infinity_t {
  struct _neo_js_value_t value;
  bool netative;
};

static const wchar_t *neo_js_infinity_typeof(neo_js_context_t ctx,
                                             neo_js_variable_t variable) {
  return L"number";
}

static neo_js_variable_t neo_js_infinity_to_string(neo_js_context_t ctx,
                                                   neo_js_variable_t self) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_infinity_t infinity =
      neo_js_value_to_infinity(neo_js_variable_get_value(self));
  neo_js_string_t string = neo_create_js_string(
      allocator, infinity->netative ? L"-Infinity" : L"Infinity");
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, neo_js_string_to_value(string)));
}

static neo_js_variable_t neo_js_infinity_to_boolean(neo_js_context_t ctx,
                                                    neo_js_variable_t self) {
  return neo_js_context_create_boolean(ctx, true);
}
static neo_js_variable_t neo_js_infinity_to_number(neo_js_context_t ctx,
                                                   neo_js_variable_t self) {
  return self;
}

neo_js_type_t neo_get_js_infinity_type() {
  static struct _neo_js_type_t type = {
      neo_js_infinity_typeof,
      neo_js_infinity_to_string,
      neo_js_infinity_to_boolean,
      neo_js_infinity_to_number,
  };
  return &type;
}

static void neo_js_infinity_dispose(neo_allocator_t allocator,
                                    neo_js_infinity_t self) {}

neo_js_infinity_t neo_create_js_infinity(neo_allocator_t allocator,
                                         bool negative) {
  neo_js_infinity_t infinity = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_infinity_t), neo_js_infinity_dispose);
  infinity->value.type = neo_get_js_infinity_type();
  infinity->netative = negative;
  return infinity;
}

neo_js_value_t neo_js_infinity_to_value(neo_js_infinity_t self) {
  return &self->value;
}

neo_js_infinity_t neo_js_value_to_infinity(neo_js_value_t value) {
  if (value->type == neo_get_js_infinity_type()) {
    return (neo_js_infinity_t)value;
  }
  return NULL;
}

bool neo_js_infinity_is_negative(neo_js_infinity_t self) {
  return self->netative;
}