#include "engine/basetype/boolean.h"
#include "core/allocator.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"

static const wchar_t *
neo_engine_boolean_typeof(neo_engine_context_t ctx,
                          neo_engine_variable_t variable) {
  return L"boolean";
}

static neo_engine_variable_t
neo_engine_boolean_to_string(neo_engine_context_t ctx,
                             neo_engine_variable_t self) {
  neo_allocator_t allocator =
      neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
  neo_engine_boolean_t boolean = neo_engine_value_to_boolean(
      neo_engine_handle_get_value(neo_engine_variable_get_handle(self)));
  neo_engine_string_t string =
      neo_create_js_string(allocator, boolean->boolean ? L"true" : L"false");
  return neo_engine_context_create_variable(
      ctx, neo_create_js_handle(allocator, &string->value));
}

static neo_engine_variable_t
neo_engine_boolean_to_boolean(neo_engine_context_t ctx,
                              neo_engine_variable_t self) {
  return self;
}

static neo_engine_variable_t
neo_engine_boolean_to_number(neo_engine_context_t ctx,
                             neo_engine_variable_t self) {
  neo_engine_boolean_t boolean = neo_engine_value_to_boolean(
      neo_engine_handle_get_value(neo_engine_variable_get_handle(self)));
  return neo_engine_context_create_number(ctx, boolean->boolean ? 1 : 0);
}

static neo_engine_variable_t
neo_engine_boolean_to_primitive(neo_engine_context_t ctx,
                                neo_engine_variable_t self) {
  return self;
}

static neo_engine_variable_t
neo_engine_boolean_to_object(neo_engine_context_t ctx,
                             neo_engine_variable_t self) {
  neo_engine_variable_t constructor =
      neo_engine_context_get_boolean_constructor(ctx);
  neo_engine_variable_t argv[] = {self};
  return neo_engine_context_construct(ctx, constructor, 1, argv);
}

static neo_engine_variable_t
neo_engine_boolean_get_field(neo_engine_context_t ctx,
                             neo_engine_variable_t object,
                             neo_engine_variable_t field) {
  return neo_engine_context_get_field(
      ctx, neo_engine_context_to_object(ctx, object), field);
}

static neo_engine_variable_t neo_engine_boolean_set_field(
    neo_engine_context_t ctx, neo_engine_variable_t object,
    neo_engine_variable_t field, neo_engine_variable_t value) {
  return neo_engine_context_set_field(
      ctx, neo_engine_context_to_object(ctx, object), field, value);
}

static neo_engine_variable_t
neo_engine_boolean_del_field(neo_engine_context_t ctx,
                             neo_engine_variable_t object,
                             neo_engine_variable_t field) {
  return neo_engine_context_del_field(
      ctx, neo_engine_context_to_object(ctx, object), field);
}

static bool neo_engine_boolean_is_equal(neo_engine_context_t ctx,
                                        neo_engine_variable_t self,
                                        neo_engine_variable_t another) {
  if (self == another) {
    return true;
  }
  neo_engine_boolean_t b1 = neo_engine_value_to_boolean(
      neo_engine_handle_get_value(neo_engine_variable_get_handle(self)));
  neo_engine_boolean_t b2 = neo_engine_value_to_boolean(
      neo_engine_handle_get_value(neo_engine_variable_get_handle(another)));
  return b1->boolean == b2->boolean;
}

static void neo_engine_boolean_copy(neo_engine_context_t ctx,
                                    neo_engine_variable_t self,
                                    neo_engine_variable_t target) {
  neo_engine_boolean_t boolean =
      neo_engine_value_to_boolean(neo_engine_variable_get_value(self));
  neo_engine_handle_t htarget = neo_engine_variable_get_handle(target);
  neo_allocator_t allocaotr =
      neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
  neo_engine_handle_set_value(
      allocaotr, htarget,
      &neo_create_js_boolean(allocaotr, boolean->boolean)->value);
}

neo_engine_type_t neo_get_js_boolean_type() {
  static struct _neo_engine_type_t type = {
      NEO_TYPE_BOOLEAN,
      neo_engine_boolean_typeof,
      neo_engine_boolean_to_string,
      neo_engine_boolean_to_boolean,
      neo_engine_boolean_to_number,
      neo_engine_boolean_to_primitive,
      neo_engine_boolean_to_object,
      neo_engine_boolean_get_field,
      neo_engine_boolean_set_field,
      neo_engine_boolean_del_field,
      neo_engine_boolean_is_equal,
      neo_engine_boolean_copy,
  };
  return &type;
}

static void neo_engine_boolean_dispose(neo_allocator_t allocator,
                                       neo_engine_boolean_t self) {}

neo_engine_boolean_t neo_create_js_boolean(neo_allocator_t allocator,
                                           bool value) {
  neo_engine_boolean_t boolean =
      neo_allocator_alloc(allocator, sizeof(struct _neo_engine_boolean_t),
                          neo_engine_boolean_dispose);
  boolean->value.type = neo_get_js_boolean_type();
  boolean->value.ref = 0;
  boolean->boolean = value;
  return boolean;
}

neo_engine_boolean_t neo_engine_value_to_boolean(neo_engine_value_t value) {
  if (value->type->kind == NEO_TYPE_BOOLEAN) {
    return (neo_engine_boolean_t)value;
  }
  return NULL;
}
