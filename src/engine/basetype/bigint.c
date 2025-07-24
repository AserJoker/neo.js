#include "engine/basetype/bigint.h"
#include "core/allocator.h"
#include "core/bigint.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <wchar.h>

static const wchar_t *neo_js_bigint_typeof(neo_js_context_t ctx,
                                           neo_js_variable_t variable) {
  return L"bigint";
}

static neo_js_variable_t neo_js_bigint_to_string(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_bigint_t bigint = neo_js_value_to_bigint(
      neo_js_handle_get_value(neo_js_variable_get_handle(self)));
  wchar_t *bigint_str = neo_bigint_to_string(bigint->bigint, 10);
  neo_js_string_t string = neo_create_js_string(allocator, bigint_str);
  neo_allocator_free(allocator, bigint_str);
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &string->value), NULL);
}

static neo_js_variable_t neo_js_bigint_to_boolean(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  neo_js_bigint_t bigint = neo_js_variable_to_bigint(self);
  return neo_js_context_create_boolean(ctx, bigint->bigint != 0);
}
static neo_js_variable_t neo_js_bigint_to_number(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_js_bigint_t bigint = neo_js_variable_to_bigint(self);
  return neo_js_context_create_number(ctx,
                                      neo_bigint_to_number(bigint->bigint));
}
static neo_js_variable_t neo_js_bigint_to_primitive(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    const wchar_t *type) {
  return self;
}
static neo_js_variable_t neo_js_bigint_to_object(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_js_variable_t bigint = neo_js_context_get_bigint_constructor(ctx);
  neo_js_variable_t prototype = neo_js_context_get_field(
      ctx, bigint, neo_js_context_create_string(ctx, L"prototype"));
  neo_js_variable_t result = neo_js_context_create_object(ctx, prototype);
  neo_js_context_set_field(
      ctx, result, neo_js_context_create_string(ctx, L"constructor"), bigint);
  neo_js_context_set_internal(ctx, result, L"[[primitive]]", self);
  return result;
}
static neo_js_variable_t neo_js_bigint_get_field(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 neo_js_variable_t field) {
  return neo_js_context_get_field(ctx, neo_js_bigint_to_object(ctx, self),
                                  field);
}
static neo_js_variable_t neo_js_bigint_set_field(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 neo_js_variable_t field,
                                                 neo_js_variable_t value) {
  return neo_js_context_set_field(ctx, neo_js_bigint_to_object(ctx, self),
                                  field, value);
}

static neo_js_variable_t neo_js_bigint_del_field(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 neo_js_variable_t field) {
  return neo_js_context_del_field(ctx, neo_js_bigint_to_object(ctx, self),
                                  field);
}

static bool neo_js_bigint_is_equal(neo_js_context_t ctx, neo_js_variable_t self,
                                   neo_js_variable_t another) {
  neo_js_value_t val1 = neo_js_variable_get_value(self);
  neo_js_value_t val2 = neo_js_variable_get_value(another);
  neo_js_bigint_t num1 = neo_js_value_to_bigint(val1);
  neo_js_bigint_t num2 = neo_js_value_to_bigint(val2);
  return neo_bigint_is_equal(num1->bigint, num2->bigint);
}
static void neo_js_bigint_copy(neo_js_context_t ctx, neo_js_variable_t self,
                               neo_js_variable_t target) {
  neo_js_bigint_t bigint =
      neo_js_value_to_bigint(neo_js_variable_get_value(self));
  neo_js_handle_t htarget = neo_js_variable_get_handle(target);
  neo_allocator_t allocaotr =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_handle_set_value(
      allocaotr, htarget,
      &neo_create_js_bigint(allocaotr, neo_bigint_clone(bigint->bigint))
           ->value);
}
neo_js_type_t neo_get_js_bigint_type() {
  static struct _neo_js_type_t type = {
      NEO_JS_TYPE_BIGINT,      neo_js_bigint_typeof,
      neo_js_bigint_to_string, neo_js_bigint_to_boolean,
      neo_js_bigint_to_number, neo_js_bigint_to_primitive,
      neo_js_bigint_to_object, neo_js_bigint_get_field,
      neo_js_bigint_set_field, neo_js_bigint_del_field,
      neo_js_bigint_is_equal,  neo_js_bigint_copy,
  };
  return &type;
}

static void neo_js_bigint_dispose(neo_allocator_t allocator,
                                  neo_js_bigint_t self) {
  neo_js_value_dispose(allocator, &self->value);
  neo_allocator_free(allocator, self->bigint);
}

neo_js_bigint_t neo_create_js_bigint(neo_allocator_t allocator,
                                     neo_bigint_t value) {
  neo_js_bigint_t bigint = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_bigint_t), neo_js_bigint_dispose);
  neo_js_value_init(allocator, &bigint->value);
  bigint->value.type = neo_get_js_bigint_type();
  bigint->bigint = value;
  return bigint;
}

neo_js_bigint_t neo_js_value_to_bigint(neo_js_value_t value) {
  if (value->type->kind == NEO_JS_TYPE_BIGINT) {
    return (neo_js_bigint_t)value;
  }
  return NULL;
}
