#include "engine/basetype/number.h"
#include "core/allocator.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <math.h>

static const wchar_t *neo_engine_number_typeof(neo_engine_context_t ctx,
                                               neo_engine_variable_t variable) {
  return L"number";
}

static neo_engine_variable_t
neo_engine_number_to_string(neo_engine_context_t ctx,
                            neo_engine_variable_t self) {
  neo_allocator_t allocator =
      neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
  neo_engine_number_t number = neo_engine_value_to_number(
      neo_engine_handle_get_value(neo_engine_variable_get_handle(self)));
  if (isnan(number->number)) {
    return neo_engine_context_create_string(ctx, L"NaN");
  }
  if (isinf(number->number)) {
    if (number->number > 0.0f) {
      return neo_engine_context_create_string(ctx, L"Infinity");
    } else {
      return neo_engine_context_create_string(ctx, L"-Infinity");
    }
  }
  wchar_t str[32];
  swprintf(str, 32, L"%lg", number->number);
  neo_engine_string_t string = neo_create_js_string(allocator, str);
  return neo_engine_context_create_variable(
      ctx, neo_create_js_handle(allocator, &string->value));
}

static neo_engine_variable_t
neo_engine_number_to_boolean(neo_engine_context_t ctx,
                             neo_engine_variable_t self) {
  neo_engine_number_t number =
      neo_engine_value_to_number(neo_engine_variable_get_value(self));
  return neo_engine_context_create_boolean(ctx, number->number != 0);
}
static neo_engine_variable_t
neo_engine_number_to_number(neo_engine_context_t ctx,
                            neo_engine_variable_t self) {
  return self;
}
static neo_engine_variable_t
neo_engine_number_to_primitive(neo_engine_context_t ctx,
                               neo_engine_variable_t self) {
  return self;
}
static neo_engine_variable_t
neo_engine_number_to_object(neo_engine_context_t ctx,
                            neo_engine_variable_t self) {
  return neo_engine_context_construct(
      ctx, neo_engine_context_get_number_constructor(ctx), 1, &self);
}
static neo_engine_variable_t
neo_engine_number_get_field(neo_engine_context_t ctx,
                            neo_engine_variable_t self,
                            neo_engine_variable_t field) {
  return neo_engine_context_get_field(
      ctx, neo_engine_number_to_object(ctx, self), field);
}
static neo_engine_variable_t neo_engine_number_set_field(
    neo_engine_context_t ctx, neo_engine_variable_t self,
    neo_engine_variable_t field, neo_engine_variable_t value) {
  return neo_engine_context_set_field(
      ctx, neo_engine_number_to_object(ctx, self), field, value);
}

static neo_engine_variable_t
neo_engine_number_del_field(neo_engine_context_t ctx,
                            neo_engine_variable_t self,
                            neo_engine_variable_t field) {
  return neo_engine_context_del_field(
      ctx, neo_engine_number_to_object(ctx, self), field);
}

static bool neo_engine_number_is_equal(neo_engine_context_t ctx,
                                       neo_engine_variable_t self,
                                       neo_engine_variable_t another) {
  neo_engine_value_t val1 = neo_engine_variable_get_value(self);
  neo_engine_value_t val2 = neo_engine_variable_get_value(another);
  neo_engine_number_t num1 = neo_engine_value_to_number(val1);
  neo_engine_number_t num2 = neo_engine_value_to_number(val2);
  if (isnan(num1->number) || isnan(num2->number)) {
    return false;
  }
  return num1->number == num2->number;
}
static void neo_engine_number_copy(neo_engine_context_t ctx,
                                   neo_engine_variable_t self,
                                   neo_engine_variable_t target) {
  neo_engine_number_t number =
      neo_engine_value_to_number(neo_engine_variable_get_value(self));
  neo_engine_handle_t htarget = neo_engine_variable_get_handle(target);
  neo_allocator_t allocaotr =
      neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
  neo_engine_handle_set_value(
      allocaotr, htarget,
      &neo_create_js_number(allocaotr, number->number)->value);
}
neo_engine_type_t neo_get_js_number_type() {
  static struct _neo_engine_type_t type = {
      NEO_TYPE_NUMBER,
      neo_engine_number_typeof,
      neo_engine_number_to_string,
      neo_engine_number_to_boolean,
      neo_engine_number_to_number,
      neo_engine_number_to_primitive,
      neo_engine_number_to_object,
      neo_engine_number_get_field,
      neo_engine_number_set_field,
      neo_engine_number_del_field,
      neo_engine_number_is_equal,
      neo_engine_number_copy,
  };
  return &type;
}

static void neo_engine_number_dispose(neo_allocator_t allocator,
                                      neo_engine_number_t self) {}

neo_engine_number_t neo_create_js_number(neo_allocator_t allocator,
                                         double value) {
  neo_engine_number_t number =
      neo_allocator_alloc(allocator, sizeof(struct _neo_engine_number_t),
                          neo_engine_number_dispose);
  number->value.type = neo_get_js_number_type();
  number->number = value;
  number->value.ref = 0;
  return number;
}

neo_engine_number_t neo_engine_value_to_number(neo_engine_value_t value) {
  if (value->type->kind == NEO_TYPE_NUMBER) {
    return (neo_engine_number_t)value;
  }
  return NULL;
}
