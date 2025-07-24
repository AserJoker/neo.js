#include "engine/basetype/undefined.h"
#include "core/allocator.h"
#include "engine/basetype/string.h"
#include "engine/basetype/symbol.h"
#include "engine/context.h"
#include "engine/handle.h"
#include "engine/runtime.h"
#include "engine/type.h"
#include "engine/value.h"
#include "engine/variable.h"
#include <math.h>

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
      ctx, neo_create_js_handle(allocator, &string->value), NULL);
}

static neo_js_variable_t neo_js_undefined_to_boolean(neo_js_context_t ctx,
                                                     neo_js_variable_t self) {
  return neo_js_context_create_boolean(ctx, false);
}
static neo_js_variable_t neo_js_undefined_to_number(neo_js_context_t ctx,
                                                    neo_js_variable_t self) {
  return neo_js_context_create_number(ctx, NAN);
}

static neo_js_variable_t neo_js_undefined_to_primitive(neo_js_context_t ctx,
                                                       neo_js_variable_t self,
                                                       const wchar_t *type) {
  return self;
}

static neo_js_variable_t neo_js_undefined_to_object(neo_js_context_t ctx,
                                                    neo_js_variable_t self) {
  return neo_js_context_create_object(ctx, NULL);
}

static neo_js_variable_t neo_js_undefined_get_field(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    neo_js_variable_t field) {
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
  if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
    swprintf(message, len,
             L"Cannot read properties of undefined (reading 'Symbol(%ls)')",
             field_name);
  } else {
    swprintf(message, len,
             L"Cannot read properties of undefined (reading '%ls')",
             field_name);
  }
  neo_js_variable_t error =
      neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
  neo_allocator_free(allocator, message);
  return error;
}
static neo_js_variable_t neo_js_undefined_set_field(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    neo_js_variable_t field,
                                                    neo_js_variable_t value) {
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
  if (neo_js_variable_get_type(field)->kind == NEO_JS_TYPE_SYMBOL) {
    swprintf(message, len,
             L"Cannot read properties of undefined (reading 'Symbol(%ls)')",
             field_name);
  } else {
    swprintf(message, len,
             L"Cannot read properties of undefined (reading '%ls')",
             field_name);
  }
  neo_js_variable_t error =
      neo_js_context_create_simple_error(ctx, NEO_JS_ERROR_TYPE, message);
  neo_allocator_free(allocator, message);
  return error;
}
static neo_js_variable_t neo_js_undefined_del_field(neo_js_context_t ctx,
                                                    neo_js_variable_t self,
                                                    neo_js_variable_t field) {
  return neo_js_context_create_simple_error(
      ctx, NEO_JS_ERROR_TYPE,
      L"Cannot convert undefined or undefined to object");
}

static bool neo_js_undefined_is_equal(neo_js_context_t ctx,
                                      neo_js_variable_t self,
                                      neo_js_variable_t another) {
  return neo_js_variable_get_type(another)->kind = NEO_JS_TYPE_UNDEFINED;
}

static void neo_js_undefined_copy(neo_js_context_t ctx, neo_js_variable_t self,
                                  neo_js_variable_t target) {

  neo_js_handle_t htarget = neo_js_variable_get_handle(target);
  neo_allocator_t allocaotr =
      neo_js_runtime_get_allocator(neo_js_context_get_runtime(ctx));
  neo_js_handle_set_value(allocaotr, htarget,
                          &neo_create_js_undefined(allocaotr)->value);
}

neo_js_type_t neo_get_js_undefined_type() {
  static struct _neo_js_type_t type = {
      NEO_JS_TYPE_UNDEFINED,      neo_js_undefined_typeof,
      neo_js_undefined_to_string, neo_js_undefined_to_boolean,
      neo_js_undefined_to_number, neo_js_undefined_to_primitive,
      neo_js_undefined_to_object, neo_js_undefined_get_field,
      neo_js_undefined_set_field, neo_js_undefined_del_field,
      neo_js_undefined_is_equal,  neo_js_undefined_copy,
  };
  return &type;
}

static void neo_js_undefined_dispose(neo_allocator_t allocator,
                                     neo_js_undefined_t self) {
  neo_js_value_dispose(allocator, &self->value);
}

neo_js_undefined_t neo_create_js_undefined(neo_allocator_t allocator) {
  neo_js_undefined_t undefined = neo_allocator_alloc(
      allocator, sizeof(struct _neo_js_undefined_t), neo_js_undefined_dispose);
  neo_js_value_init(allocator, &undefined->value);
  undefined->value.type = neo_get_js_undefined_type();
  return undefined;
}
neo_js_undefined_t neo_js_value_to_undefined(neo_js_value_t value) {
  if (value->type->kind == NEO_JS_TYPE_UNDEFINED) {
    return (neo_js_undefined_t)value;
  }
  return NULL;
}
