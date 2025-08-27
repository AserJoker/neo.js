#include "engine/basetype/null.h"
#include "core/allocator.h"
#include "engine/basetype/string.h"
#include "engine/basetype/symbol.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <wchar.h>

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
      ctx, neo_create_js_handle(allocator, &string->value), NULL);
}

static neo_js_variable_t neo_js_null_to_boolean(neo_js_context_t ctx,
                                                neo_js_variable_t self) {
  return neo_js_context_create_boolean(ctx, false);
}

static neo_js_variable_t neo_js_null_to_number(neo_js_context_t ctx,
                                               neo_js_variable_t self) {
  return neo_js_context_create_number(ctx, 0);
}

static neo_js_variable_t neo_js_null_to_primitive(neo_js_context_t ctx,
                                                  neo_js_variable_t self,
                                                  const wchar_t *type) {
  return self;
}

static neo_js_variable_t neo_js_null_to_object(neo_js_context_t ctx,
                                               neo_js_variable_t self) {
  return neo_js_context_create_object(ctx, NULL);
}

static neo_js_variable_t neo_js_null_get_field(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               neo_js_variable_t field,
                                               neo_js_variable_t receiver) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  const wchar_t *field_name = NULL;
  if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
    neo_js_symbol_t symbol =
        neo_js_value_to_symbol(neo_js_variable_get_value(field));
    field_name = symbol->description;
  } else {
    neo_js_variable_t valstring = neo_js_context_to_string(ctx, field);
    neo_js_value_t value = neo_js_variable_get_value(valstring);
    neo_js_string_t string = neo_js_value_to_string(value);
    field_name = string->string;
  }
  size_t len = wcslen(field_name) + 64;
  wchar_t *message = neo_allocator_alloc(
      allocator, sizeof(wchar_t) * (wcslen(field_name) + 64), NULL);
  swprintf(message, len, L"Cannot read properties of null (reading '%ls')",
           field_name);
  neo_js_variable_t error =
      neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
  neo_allocator_free(allocator, message);
  return error;
}
static neo_js_variable_t neo_js_null_set_field(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               neo_js_variable_t field,
                                               neo_js_variable_t value,
                                               neo_js_variable_t receiver) {
  neo_allocator_t allocator =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  const wchar_t *field_name = NULL;
  if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
    neo_js_symbol_t symbol =
        neo_js_value_to_symbol(neo_js_variable_get_value(field));
    field_name = symbol->description;
  } else {
    neo_js_variable_t valstring = neo_js_context_to_string(ctx, field);
    neo_js_value_t value = neo_js_variable_get_value(valstring);
    neo_js_string_t string = neo_js_value_to_string(value);
    field_name = string->string;
  }
  size_t len = wcslen(field_name) + 64;
  wchar_t *message = neo_allocator_alloc(
      allocator, sizeof(wchar_t) * (wcslen(field_name) + 64), NULL);
  swprintf(message, len, L"Cannot set properties of null (reading '%ls')",
           field_name);
  neo_js_variable_t error =
      neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
  neo_allocator_free(allocator, message);
  return error;
}
static neo_js_variable_t neo_js_null_del_field(neo_js_context_t ctx,
                                               neo_js_variable_t self,
                                               neo_js_variable_t field) {
  return neo_js_context_create_simple_error(
      ctx, NEO_JS_ERROR_TYPE, L"Cannot convert undefined or null to object");
}

static bool neo_js_null_is_equal(neo_js_context_t ctx, neo_js_variable_t self,
                                 neo_js_variable_t another) {
  return neo_js_variable_get_type(another)->kind == NEO_JS_TYPE_NULL;
}
static neo_js_variable_t neo_js_null_copy(neo_js_context_t ctx,
                                          neo_js_variable_t self,
                                          neo_js_variable_t target) {

  neo_js_handle_t htarget = neo_js_variable_get_handle(target);
  neo_allocator_t allocaotr =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_handle_set_value(allocaotr, htarget,
                          &neo_create_js_null(allocaotr)->value);
  return target;
}
neo_js_type_t neo_get_js_null_type() {
  static struct _neo_js_type_t type = {
      NEO_JS_TYPE_NULL,       neo_js_null_typeof,    neo_js_null_to_string,
      neo_js_null_to_boolean, neo_js_null_to_number, neo_js_null_to_primitive,
      neo_js_null_to_object,  neo_js_null_get_field, neo_js_null_set_field,
      neo_js_null_del_field,  neo_js_null_is_equal,  neo_js_null_copy,
  };
  return &type;
}

static void neo_js_null_dispose(neo_allocator_t allocator, neo_js_null_t self) {
  neo_js_value_dispose(allocator, &self->value);
}

neo_js_null_t neo_create_js_null(neo_allocator_t allocator) {
  neo_js_null_t null = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_null_t), neo_js_null_dispose);
  neo_js_value_init(allocator, &null->value);
  null->value.type = neo_get_js_null_type();
  return null;
}

neo_js_null_t neo_js_value_to_null(neo_js_value_t value) {
  if (value->type->kind == NEO_JS_TYPE_NULL) {
    return (neo_js_null_t)value;
  }
  return NULL;
}
