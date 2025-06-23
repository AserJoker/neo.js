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

static const wchar_t *neo_engine_null_typeof(neo_engine_context_t ctx,
                                             neo_engine_variable_t variable) {
  return L"object";
}

static neo_engine_variable_t
neo_engine_null_to_string(neo_engine_context_t ctx,
                          neo_engine_variable_t self) {
  neo_allocator_t allocator =
      neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
  neo_engine_string_t string = neo_create_js_string(allocator, L"null");
  return neo_engine_context_create_variable(
      ctx, neo_create_js_handle(allocator, &string->value));
}

static neo_engine_variable_t
neo_engine_null_to_boolean(neo_engine_context_t ctx,
                           neo_engine_variable_t self) {
  return neo_engine_context_create_boolean(ctx, false);
}

static neo_engine_variable_t
neo_engine_null_to_number(neo_engine_context_t ctx,
                          neo_engine_variable_t self) {
  return neo_engine_context_create_number(ctx, 0);
}

static neo_engine_variable_t
neo_engine_null_to_primitive(neo_engine_context_t ctx,
                             neo_engine_variable_t self) {
  return self;
}

static neo_engine_variable_t
neo_engine_null_to_object(neo_engine_context_t ctx,
                          neo_engine_variable_t self) {
  return neo_engine_context_create_object(ctx, NULL, NULL);
}

static neo_engine_variable_t
neo_engine_null_get_field(neo_engine_context_t ctx, neo_engine_variable_t self,
                          neo_engine_variable_t field) {
  neo_allocator_t allocator =
      neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
  const wchar_t *field_name = NULL;
  if (neo_engine_variable_get_type(field)->kind == NEO_TYPE_SYMBOL) {
    neo_engine_symbol_t symbol =
        neo_engine_value_to_symbol(neo_engine_variable_get_value(field));
    field_name = symbol->description;
  } else {
    neo_engine_variable_t valstring = neo_engine_context_to_string(ctx, field);
    neo_engine_value_t value = neo_engine_variable_get_value(valstring);
    neo_engine_string_t string = neo_engine_value_to_string(value);
    field_name = string->string;
  }
  size_t len = wcslen(field_name) + 64;
  wchar_t *message = neo_allocator_alloc(
      allocator, sizeof(wchar_t) * (wcslen(field_name) + 64), NULL);
  swprintf(message, len, L"Cannot read properties of null (reading '%ls')",
           field_name);
  neo_engine_variable_t error =
      neo_engine_context_create_error(ctx, L"TypeError", message);
  neo_allocator_free(allocator, message);
  return error;
}
static neo_engine_variable_t
neo_engine_null_set_field(neo_engine_context_t ctx, neo_engine_variable_t self,
                          neo_engine_variable_t field,
                          neo_engine_variable_t value) {
  neo_allocator_t allocator =
      neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
  const wchar_t *field_name = NULL;
  if (neo_engine_variable_get_type(field)->kind == NEO_TYPE_SYMBOL) {
    neo_engine_symbol_t symbol =
        neo_engine_value_to_symbol(neo_engine_variable_get_value(field));
    field_name = symbol->description;
  } else {
    neo_engine_variable_t valstring = neo_engine_context_to_string(ctx, field);
    neo_engine_value_t value = neo_engine_variable_get_value(valstring);
    neo_engine_string_t string = neo_engine_value_to_string(value);
    field_name = string->string;
  }
  size_t len = wcslen(field_name) + 64;
  wchar_t *message = neo_allocator_alloc(
      allocator, sizeof(wchar_t) * (wcslen(field_name) + 64), NULL);
  swprintf(message, len, L"Cannot set properties of null (reading '%ls')",
           field_name);
  neo_engine_variable_t error =
      neo_engine_context_create_error(ctx, L"TypeError", message);
  neo_allocator_free(allocator, message);
  return error;
}
static neo_engine_variable_t
neo_engine_null_del_field(neo_engine_context_t ctx, neo_engine_variable_t self,
                          neo_engine_variable_t field) {
  return neo_engine_context_create_error(
      ctx, L"TypeError", L"Cannot convert undefined or null to object");
}

static neo_engine_variable_t neo_engine_null_def_field(
    neo_engine_context_t ctx, neo_engine_variable_t object,
    neo_engine_variable_t field, neo_engine_variable_t value, bool configurable,
    bool enumable, bool writable) {
  return neo_engine_context_create_error(
      ctx, L"TypeError", L"Cannot convert undefined or null to object");
}

static neo_engine_variable_t neo_engine_null_def_accessor(
    neo_engine_context_t ctx, neo_engine_variable_t object,
    neo_engine_variable_t field, neo_engine_variable_t getter,
    neo_engine_variable_t setter, bool configurable, bool enumable) {
  return neo_engine_context_create_error(
      ctx, L"TypeError", L"Cannot convert undefined or null to object");
}

static bool neo_engine_null_is_equal(neo_engine_context_t ctx,
                                     neo_engine_variable_t self,
                                     neo_engine_variable_t another) {
  return neo_engine_variable_get_type(another)->kind == NEO_TYPE_NULL;
}
static void neo_engine_null_copy(neo_engine_context_t ctx,
                                 neo_engine_variable_t self,
                                 neo_engine_variable_t target) {

  neo_engine_handle_t htarget = neo_engine_variable_get_handle(target);
  neo_allocator_t allocaotr =
      neo_engine_runtime_get_allocator(neo_engine_context_get_runtime(ctx));
  neo_engine_handle_set_value(allocaotr, htarget,
                              &neo_create_js_null(allocaotr)->value);
}
neo_engine_type_t neo_get_js_null_type() {
  static struct _neo_engine_type_t type = {
      NEO_TYPE_NULL,
      neo_engine_null_typeof,
      neo_engine_null_to_string,
      neo_engine_null_to_boolean,
      neo_engine_null_to_number,
      neo_engine_null_to_primitive,
      neo_engine_null_to_object,
      neo_engine_null_get_field,
      neo_engine_null_set_field,
      neo_engine_null_del_field,
      neo_engine_null_is_equal,
      neo_engine_null_copy,
  };
  return &type;
}

static void neo_engine_null_dispose(neo_allocator_t allocator,
                                    neo_engine_null_t self) {}

neo_engine_null_t neo_create_js_null(neo_allocator_t allocator) {
  neo_engine_null_t null = neo_allocator_alloc(
      allocator, sizeof(struct _neo_engine_null_t), neo_engine_null_dispose);
  null->value.type = neo_get_js_null_type();
  null->value.ref = 0;
  return null;
}

neo_engine_null_t neo_engine_value_to_null(neo_engine_value_t value) {
  if (value->type->kind == NEO_TYPE_NULL) {
    return (neo_engine_null_t)value;
  }
  return NULL;
}
