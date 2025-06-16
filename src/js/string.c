#include "js/string.h"
#include "core/allocator.h"
#include "js/context.h"
#include "js/runtime.h"
#include "js/type.h"
#include "js/value.h"
#include "js/variable.h"
#include <math.h>
#include <stdlib.h>
#include <wchar.h>

static const wchar_t *neo_js_string_typeof(neo_js_context_t ctx,
                                           neo_js_variable_t variable) {
  return L"string";
}

static neo_js_variable_t neo_js_string_to_string(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  return self;
}

static neo_js_variable_t neo_js_string_to_boolean(neo_js_context_t ctx,
                                                  neo_js_variable_t self) {
  neo_js_string_t string =
      neo_js_value_to_string(neo_js_variable_get_value(self));
  return neo_js_context_create_boolean(ctx, string->string[0] != 0);
}

static neo_js_variable_t neo_js_string_to_number(neo_js_context_t ctx,
                                                 neo_js_variable_t self) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
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

neo_js_type_t neo_get_js_string_type() {
  static struct _neo_js_type_t type = {
      neo_js_string_typeof,
      neo_js_string_to_string,
      neo_js_string_to_boolean,
      neo_js_string_to_number,
  };
  return &type;
}

static void neo_js_string_dispose(neo_allocator_t allocator,
                                  neo_js_string_t self) {
  neo_allocator_free(allocator, self->string);
}

neo_js_string_t neo_create_js_string(neo_allocator_t allocator,
                                     const wchar_t *value) {
  if (!value) {
    value = L"";
  }
  size_t len = wcslen(value);
  neo_js_string_t self = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_string_t), neo_js_string_dispose);
  self->value.type = neo_get_js_string_type();
  self->string =
      neo_allocator_alloc(allocator, (len + 1) * sizeof(wchar_t), NULL);
  wcscpy(self->string, value);
  self->string[len] = 0;
  return self;
}

neo_js_string_t neo_js_value_to_string(neo_js_value_t value) {
  if (value->type == neo_get_js_string_type()) {
    return (neo_js_string_t)value;
  }
  return NULL;
}
