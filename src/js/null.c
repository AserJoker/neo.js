#include "js/null.h"
#include "core/allocator.h"
#include "js/context.h"
#include "js/handle.h"
#include "js/runtime.h"
#include "js/string.h"
#include "js/type.h"
#include "js/value.h"
#include "js/variable.h"

static const wchar_t *neo_js_null_typeof(neo_js_context_t ctx,
                                         neo_js_variable_t variable) {
  return L"object";
}

static neo_js_variable_t neo_js_null_to_string(neo_js_context_t ctx,
                                               neo_js_variable_t self) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_string_t string = neo_create_js_string(allocator, L"null");
  return neo_js_context_create_variable(
      ctx, neo_create_js_handle(allocator, &string->value));
}

static neo_js_variable_t neo_js_null_to_boolean(neo_js_context_t ctx,
                                                neo_js_variable_t self) {
  return neo_js_context_create_boolean(ctx, false);
}
static neo_js_variable_t neo_js_null_to_number(neo_js_context_t ctx,
                                               neo_js_variable_t self) {
  return neo_js_context_create_number(ctx, 0);
}

neo_js_type_t neo_get_js_null_type() {
  static struct _neo_js_type_t type = {
      neo_js_null_typeof,
      neo_js_null_to_string,
      neo_js_null_to_boolean,
      neo_js_null_to_number,
  };
  return &type;
}

static void neo_js_null_dispose(neo_allocator_t allocator, neo_js_null_t self) {
}

neo_js_null_t neo_create_js_null(neo_allocator_t allocator) {
  neo_js_null_t null = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_null_t), neo_js_null_dispose);
  null->value.type = neo_get_js_null_type();
  return null;
}

neo_js_null_t neo_js_value_to_null(neo_js_value_t value) {
  if (value->type == neo_get_js_null_type()) {
    return (neo_js_null_t)value;
  }
  return NULL;
}
