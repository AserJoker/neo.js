#include "js/undefined.h"
#include "core/allocator.h"
#include "js/context.h"
#include "js/handle.h"
#include "js/runtime.h"
#include "js/string.h"
#include "js/type.h"
#include "js/value.h"
#include "js/variable.h"

struct _neo_js_undefined_t {
  struct _neo_js_value_t value;
  bool undefined;
};

static const wchar_t *neo_js_undefined_typeof(neo_js_context_t ctx,
                                              neo_js_variable_t variable) {
  return L"undefined";
}

static neo_js_variable_t neo_js_undefined_to_string(neo_js_context_t ctx,
                                                    neo_js_variable_t self) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_string_t string = neo_create_js_string(allocator, L"undefined");
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, neo_js_string_to_value(string)));
}

static neo_js_variable_t neo_js_undefined_to_boolean(neo_js_context_t ctx,
                                                     neo_js_variable_t self) {
  return neo_js_context_create_boolean(ctx, false);
}

neo_js_type_t neo_get_js_undefined_type() {
  static struct _neo_js_type_t type = {
      neo_js_undefined_typeof,
      neo_js_undefined_to_string,
      neo_js_undefined_to_boolean,
  };
  return &type;
}

static void neo_js_undefined_dispose(neo_allocator_t allocator,
                                     neo_js_undefined_t self) {}

neo_js_undefined_t neo_create_js_undefined(neo_allocator_t allocator) {
  neo_js_undefined_t undefined = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_undefined_t), neo_js_undefined_dispose);
  undefined->value.type = neo_get_js_undefined_type();
  return undefined;
}

neo_js_value_t neo_js_undefined_to_value(neo_js_undefined_t self) {
  return &self->value;
}

neo_js_undefined_t neo_js_value_to_undefined(neo_js_value_t value) {
  if (value->type == neo_get_js_undefined_type()) {
    return (neo_js_undefined_t)value;
  }
  return NULL;
}
