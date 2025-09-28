#include "engine/basetype/boolean.h"
#include "core/allocator.h"
#include "engine/basetype/string.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <wchar.h>


static const char *neo_js_boolean_typeof(neo_js_context_t ctx,
                                         neo_js_variable_t variable) {
  return "boolean";
}

static neo_js_variable_t neo_js_boolean_to_string(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_boolean_t boolean = neo_js_value_to_boolean(
      neo_js_handle_get_value(neo_js_variable_get_handle(self)));
  neo_js_string_t string =
      neo_create_js_string(allocator, boolean->boolean ? "true" : "false");
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &string->value), NULL);
}

static neo_js_variable_t neo_js_boolean_to_boolean(neo_js_context_t ctx,
                                                   neo_js_variable_t self) {
  neo_js_boolean_t boolean = neo_js_variable_to_boolean(self);
  return neo_js_context_create_boolean(ctx, boolean->boolean);
}

static neo_js_variable_t neo_js_boolean_to_number(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  neo_js_boolean_t boolean = neo_js_value_to_boolean(
      neo_js_handle_get_value(neo_js_variable_get_handle(self)));
  return neo_js_context_create_number(ctx, boolean->boolean ? 1 : 0);
}
static neo_js_variable_t neo_js_boolean_to_object(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  neo_js_variable_t constructor =
      neo_js_context_get_std(ctx).boolean_constructor;
  neo_js_variable_t argv[] = {self};
  return neo_js_context_construct(ctx, constructor, 1, argv);
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

static neo_js_variable_t neo_js_boolean_copy(neo_js_context_t ctx,
                                             neo_js_variable_t self,
                                             neo_js_variable_t target) {
  neo_js_boolean_t boolean =
      neo_js_value_to_boolean(neo_js_variable_get_value(self));
  neo_js_handle_t htarget = neo_js_variable_get_handle(target);
  neo_allocator_t allocaotr =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_handle_set_value(
      allocaotr, htarget,
      &neo_create_js_boolean(allocaotr, boolean->boolean)->value);
  return target;
}

neo_js_type_t neo_get_js_boolean_type() {
  static struct _neo_js_type_t type = {
      NEO_JS_TYPE_BOOLEAN,
      neo_js_boolean_typeof,
      neo_js_boolean_to_string,
      neo_js_boolean_to_boolean,
      neo_js_boolean_to_number,
      NULL,
      neo_js_boolean_to_object,
      NULL,
      NULL,
      NULL,
      neo_js_boolean_is_equal,
      neo_js_boolean_copy,
  };
  return &type;
}

static void neo_js_boolean_dispose(neo_allocator_t allocator,
                                   neo_js_boolean_t self) {
  neo_js_value_dispose(allocator, &self->value);
}

neo_js_boolean_t neo_create_js_boolean(neo_allocator_t allocator, bool value) {
  neo_js_boolean_t boolean = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_boolean_t), neo_js_boolean_dispose);
  neo_js_value_init(allocator, &boolean->value);
  boolean->value.type = neo_get_js_boolean_type();
  boolean->boolean = value;
  return boolean;
}

neo_js_boolean_t neo_js_value_to_boolean(neo_js_value_t value) {
  if (value->type->kind == NEO_JS_TYPE_BOOLEAN) {
    return (neo_js_boolean_t)value;
  }
  return NULL;
}
