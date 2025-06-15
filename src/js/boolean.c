#include "js/boolean.h"
#include "core/allocator.h"
#include "js/context.h"
#include "js/handle.h"
#include "js/runtime.h"
#include "js/string.h"
#include "js/type.h"
#include "js/value.h"
#include "js/variable.h"

static const wchar_t *neo_js_boolean_typeof(neo_js_context_t ctx,
                                            neo_js_variable_t variable) {
  return L"boolean";
}

static neo_js_variable_t neo_js_boolean_to_string(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_boolean_t boolean = neo_js_value_to_boolean(
      neo_js_handle_get_value(neo_js_variable_get_handle(self)));
  neo_js_string_t string =
      neo_create_js_string(allocator, boolean->boolean ? L"true" : L"false");
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &string->value));
}

static neo_js_variable_t neo_js_boolean_to_boolean(neo_js_context_t ctx,
                                                   neo_js_variable_t self) {
  return self;
}

static neo_js_variable_t neo_js_boolean_to_number(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  neo_js_boolean_t boolean = neo_js_value_to_boolean(
      neo_js_handle_get_value(neo_js_variable_get_handle(self)));
  return neo_js_context_create_number(ctx, boolean->boolean ? 1 : 0);
}

static neo_js_variable_t neo_js_boolean_to_primitive(neo_js_context_t ctx,
                                                     neo_js_variable_t self) {
  return self;
}

static neo_js_variable_t neo_js_boolean_to_object(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  neo_js_variable_t constructor = neo_js_context_get_boolean_constructor(ctx);
  neo_js_variable_t argv[] = {self};
  return neo_js_context_construct(ctx, constructor, 1, argv);
}

static neo_js_variable_t neo_js_boolean_get_field(neo_js_context_t ctx,
                                                  neo_js_variable_t object,
                                                  neo_js_variable_t field) {
  return neo_js_context_get_field(ctx, neo_js_context_to_object(ctx, object),
                                  field);
}

static neo_js_variable_t neo_js_boolean_set_field(neo_js_context_t ctx,
                                                  neo_js_variable_t object,
                                                  neo_js_variable_t field,
                                                  neo_js_variable_t value) {
  return neo_js_context_set_field(ctx, neo_js_context_to_object(ctx, object),
                                  field, value);
}

static neo_js_variable_t neo_js_boolean_del_field(neo_js_context_t ctx,
                                                  neo_js_variable_t object,
                                                  neo_js_variable_t field) {
  return neo_js_context_del_field(ctx, neo_js_context_to_object(ctx, object),
                                  field);
}

static bool neo_js_boolean_is_equal(neo_js_context_t ctx,
                                    neo_js_variable_t self,
                                    neo_js_variable_t another) {
  if (self == another) {
    return true;
  }
  neo_js_boolean_t b1 = neo_js_value_to_boolean(
      neo_js_handle_get_value(neo_js_variable_get_handle(self)));
  neo_js_boolean_t b2 = neo_js_value_to_boolean(
      neo_js_handle_get_value(neo_js_variable_get_handle(another)));
  return b1->boolean == b2->boolean;
}

neo_js_type_t neo_get_js_boolean_type() {
  static struct _neo_js_type_t type = {
      neo_js_boolean_typeof,       neo_js_boolean_to_string,
      neo_js_boolean_to_boolean,   neo_js_boolean_to_number,
      neo_js_boolean_to_primitive, neo_js_boolean_to_object,
      neo_js_boolean_get_field,    neo_js_boolean_set_field,
      neo_js_boolean_del_field,    neo_js_boolean_is_equal,
  };
  return &type;
}

static void neo_js_boolean_dispose(neo_allocator_t allocator,
                                   neo_js_boolean_t self) {}

neo_js_boolean_t neo_create_js_boolean(neo_allocator_t allocator, bool value) {
  neo_js_boolean_t boolean = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_boolean_t), neo_js_boolean_dispose);
  boolean->value.type = neo_get_js_boolean_type();
  boolean->boolean = value;
  return boolean;
}

neo_js_boolean_t neo_js_value_to_boolean(neo_js_value_t value) {
  if (value->type == neo_get_js_boolean_type()) {
    return (neo_js_boolean_t)value;
  }
  return NULL;
}
