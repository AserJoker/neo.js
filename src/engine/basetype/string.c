#include "engine/basetype/string.h"
#include "core/allocator.h"
#include "engine/context.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <math.h>
#include <stdlib.h>
#include <wchar.h>

static const wchar_t *neo_js_string_typeof(neo_js_context_t ctx,
                                           neo_js_variable_t variable) {
  return L"string";
}

static neo_js_variable_t neo_js_string_to_string(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_js_string_t string = neo_js_variable_to_string(self);
  return neo_js_context_create_string(ctx, string->string);
}

static neo_js_variable_t neo_js_string_to_boolean(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  neo_js_string_t string = neo_js_variable_to_string(self);
  return neo_js_context_create_boolean(ctx, string->string[0] != 0);
}

static neo_js_variable_t neo_js_string_to_number(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_js_string_t string =
      neo_js_value_to_string(neo_js_variable_get_value(self));
  if (wcscmp(string->string, L"Infinity") == 0) {
    return neo_js_context_create_number(ctx, INFINITY);
  }
  if (wcscmp(string->string, L"-Infinity") == 0) {
    return neo_js_context_create_number(ctx, -INFINITY);
  }
  wchar_t *end = 0;
  double val = wcstod(string->string, &end);
  if (*end != 0) {
    return neo_js_context_create_number(ctx, NAN);
  }
  return neo_js_context_create_number(ctx, val);
}

static neo_js_variable_t neo_js_string_to_primitive(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    const wchar_t *type) {
  return self;
}

static neo_js_variable_t neo_js_string_to_object(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  return neo_js_context_construct(
      ctx, neo_js_context_get_string_constructor(ctx), 1, &self);
}

static neo_js_variable_t neo_js_string_get_field(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 neo_js_variable_t field) {
  if (neo_js_variable_get_type(field)->kind == NEO_TYPE_STRING) {
    neo_js_string_t string =
        neo_js_value_to_string(neo_js_variable_get_value(field));
    if (wcscmp(string->string, L"length") == 0) {
      return neo_js_context_create_number(ctx, wcslen(string->string));
    }
  }
  return neo_js_context_get_field(ctx, neo_js_string_to_object(ctx, self),
                                  field);
}

static neo_js_variable_t neo_js_string_set_field(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 neo_js_variable_t field,
                                                 neo_js_variable_t value) {
  if (neo_js_variable_get_type(field)->kind == NEO_TYPE_STRING) {
    neo_js_string_t string =
        neo_js_value_to_string(neo_js_variable_get_value(field));
    if (wcscmp(string->string, L"length") == 0) {
      return neo_js_context_create_undefined(ctx);
    }
  }
  return neo_js_context_set_field(ctx, neo_js_string_to_object(ctx, self),
                                  field, value);
}

static neo_js_variable_t neo_js_string_del_field(neo_js_context_t ctx,
                                                 neo_js_variable_t self,
                                                 neo_js_variable_t field) {
  if (neo_js_variable_get_type(field)->kind == NEO_TYPE_STRING) {
    neo_js_string_t string =
        neo_js_value_to_string(neo_js_variable_get_value(field));
    if (wcscmp(string->string, L"length") == 0) {
      return neo_js_context_create_boolean(ctx, false);
    }
  }
  return neo_js_context_del_field(ctx, neo_js_string_to_object(ctx, self),
                                  field);
}

static bool neo_js_string_is_equal(neo_js_context_t ctx, neo_js_variable_t self,
                                   neo_js_variable_t another) {
  neo_js_value_t val1 = neo_js_variable_get_value(self);
  neo_js_value_t val2 = neo_js_variable_get_value(another);
  neo_js_string_t str1 = neo_js_value_to_string(val1);
  neo_js_string_t str2 = neo_js_value_to_string(val2);
  return wcscmp(str1->string, str2->string) == 0;
}
static void neo_js_string_copy(neo_js_context_t ctx, neo_js_variable_t self,
                               neo_js_variable_t target) {
  neo_js_string_t string =
      neo_js_value_to_string(neo_js_variable_get_value(self));
  neo_js_handle_t htarget = neo_js_variable_get_handle(target);
  neo_allocator_t allocaotr =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_handle_set_value(
      allocaotr, htarget,
      &neo_create_js_string(allocaotr, string->string)->value);
}
neo_js_type_t neo_get_js_string_type() {
  static struct _neo_js_type_t type = {
      NEO_TYPE_STRING,         neo_js_string_typeof,
      neo_js_string_to_string, neo_js_string_to_boolean,
      neo_js_string_to_number, neo_js_string_to_primitive,
      neo_js_string_to_object, neo_js_string_get_field,
      neo_js_string_set_field, neo_js_string_del_field,
      neo_js_string_is_equal,  neo_js_string_copy,
  };
  return &type;
}

static void neo_js_string_dispose(neo_allocator_t allocator,
                                  neo_js_string_t self) {
  neo_allocator_free(allocator, self->string);
  neo_js_value_dispose(allocator, &self->value);
}

neo_js_string_t neo_create_js_string(neo_allocator_t allocator,
                                     const wchar_t *value) {
  if (!value) {
    value = L"";
  }
  size_t len = wcslen(value);
  neo_js_string_t string = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_string_t), neo_js_string_dispose);
  neo_js_value_init(allocator, &string->value);
  string->value.type = neo_get_js_string_type();
  string->string =
      neo_allocator_alloc(allocator, (len + 1) * sizeof(wchar_t), NULL);
  string->string[0] = 0;
  wcscpy(string->string, value);
  string->string[len] = 0;
  return string;
}

neo_js_string_t neo_js_value_to_string(neo_js_value_t value) {
  if (value->type->kind == NEO_TYPE_STRING) {
    return (neo_js_string_t)value;
  }
  return NULL;
}
